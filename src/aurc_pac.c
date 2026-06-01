#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <dirent.h>
#include <alpm.h>
#include <alpm_list.h>
#include "constants.h"
#include "colors.h"

int existingAurPackage(const char *packageName);
void installAurPackages(char **packageNames, unsigned int numPackages);
void upgradeAurPackages(void);

/* ── libalpm init ────────────────────────────────────────────────────────── */

static void expandUrl(const char *tmpl, const char *repo, const char *arch,
                      char *out, size_t size)
{
    size_t o = 0;
    for (size_t i = 0; tmpl[i] && o < size - 1; i++)
    {
        if (strncmp(tmpl + i, "$repo", 5) == 0)
        {
            size_t l = strlen(repo);
            if (o + l < size - 1) { memcpy(out + o, repo, l); o += l; }
            i += 4;
        }
        else if (strncmp(tmpl + i, "$arch", 5) == 0)
        {
            size_t l = strlen(arch);
            if (o + l < size - 1) { memcpy(out + o, arch, l); o += l; }
            i += 4;
        }
        else
            out[o++] = tmpl[i];
    }
    out[o] = '\0';
}

static void addServersFromFile(alpm_db_t *db, const char *repo,
                               const char *arch, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line; while (*key == ' ' || *key == '\t') key++;
        char *val = eq + 1; while (*val == ' ' || *val == '\t') val++;
        char *ve = val + strlen(val) - 1;
        while (ve > val && (*ve == '\n' || *ve == '\r' || *ve == ' ')) *ve-- = '\0';
        char *ke = key + strlen(key) - 1;
        while (ke > key && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
        if (strcmp(key, "Server") == 0)
        {
            char expanded[512];
            expandUrl(val, repo, arch, expanded, sizeof(expanded));
            alpm_db_add_server(db, expanded);
        }
    }
    fclose(f);
}

static alpm_handle_t *alpmInit(void)
{
    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize("/", "/var/lib/pacman", &err);
    if (!handle)
    {
        fprintf(stderr, RED "Failed to initialize package manager: %s\n" RESET,
                alpm_strerror(err));
        return NULL;
    }

    struct utsname uts;
    uname(&uts);
    const char *arch = uts.machine;
    alpm_option_set_gpgdir(handle, "/etc/pacman.d/gnupg");
    alpm_list_t *archs = alpm_list_add(NULL, strdup(arch));
    alpm_option_set_architectures(handle, archs);

    FILE *conf = fopen("/etc/pacman.conf", "r");
    if (!conf)
    {
        /* Fallback: register repos found on disk (no server URLs — read ops only) */
        DIR *dir = opendir("/var/lib/pacman/sync");
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                size_t len = strlen(entry->d_name);
                if (len > 3 && strcmp(entry->d_name + len - 3, ".db") == 0)
                {
                    char name[256];
                    snprintf(name, sizeof(name), "%.*s", (int)(len - 3), entry->d_name);
                    alpm_register_syncdb(handle, name, ALPM_SIG_USE_DEFAULT);
                }
            }
            closedir(dir);
        }
        return handle;
    }

    char line[512], section[64] = "";
    alpm_db_t *db = NULL;
    while (fgets(line, sizeof(line), conf))
    {
        char *end = line + strlen(line) - 1;
        while (end > line && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        if (line[0] == '[')
        {
            char *close = strchr(line + 1, ']');
            if (!close) continue;
            *close = '\0';
            strncpy(section, line + 1, sizeof(section) - 1);
            db = (strcmp(section, "options") == 0)
                     ? NULL
                     : alpm_register_syncdb(handle, section, ALPM_SIG_USE_DEFAULT);
            continue;
        }
        if (!db) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line; while (*key == ' ' || *key == '\t') key++;
        char *val = eq + 1; while (*val == ' ' || *val == '\t') val++;
        char *ke = key + strlen(key) - 1; while (ke > key && *ke == ' ') *ke-- = '\0';

        if (strcmp(key, "Server") == 0)
        {
            char expanded[512];
            expandUrl(val, section, arch, expanded, sizeof(expanded));
            alpm_db_add_server(db, expanded);
        }
        else if (strcmp(key, "Include") == 0)
            addServersFromFile(db, section, arch, val);
    }
    fclose(conf);
    return handle;
}

/* ── callbacks ───────────────────────────────────────────────────────────── */

static void onEvent(void *ctx, alpm_event_t *e)
{
    (void)ctx;
    if (e->type == ALPM_EVENT_PACKAGE_OPERATION_START)
    {
        alpm_event_package_operation_t *pe = (void *)e;
        switch (pe->operation)
        {
        case ALPM_PACKAGE_INSTALL:
            printf(GREEN "installing" RESET " %s %s\n",
                   alpm_pkg_get_name(pe->newpkg), alpm_pkg_get_version(pe->newpkg));
            break;
        case ALPM_PACKAGE_UPGRADE:
            printf(YELLOW "upgrading" RESET " %s (%s -> %s)\n",
                   alpm_pkg_get_name(pe->newpkg),
                   alpm_pkg_get_version(pe->oldpkg),
                   alpm_pkg_get_version(pe->newpkg));
            break;
        case ALPM_PACKAGE_REMOVE:
            printf(RED "removing" RESET " %s %s\n",
                   alpm_pkg_get_name(pe->oldpkg), alpm_pkg_get_version(pe->oldpkg));
            break;
        default:
            break;
        }
    }
    else if (e->type == ALPM_EVENT_PACKAGE_OPERATION_DONE)
        printf("\n");
}

static void onQuestion(void *ctx, alpm_question_t *q)
{
    (void)ctx;
    switch (q->type)
    {
    case ALPM_QUESTION_REPLACE_PKG:
    {
        alpm_question_replace_t *qr = (void *)q;
        printf("Replace %s with %s/%s? (y/n): ",
               alpm_pkg_get_name(qr->oldpkg),
               alpm_db_get_name(qr->newdb),
               alpm_pkg_get_name(qr->newpkg));
        char buf[8] = {'n'}; if (!fgets(buf, sizeof(buf), stdin)) buf[0] = 'n';
        qr->replace = (tolower(buf[0]) == 'y');
        break;
    }
    case ALPM_QUESTION_CONFLICT_PKG:
    {
        alpm_question_conflict_t *qc = (void *)q;
        printf(YELLOW "Conflict: %s vs %s. Remove %s? (y/n): " RESET,
               alpm_pkg_get_name(qc->conflict->package1),
               alpm_pkg_get_name(qc->conflict->package2),
               alpm_pkg_get_name(qc->conflict->package2));
        char buf[8] = {'n'}; if (!fgets(buf, sizeof(buf), stdin)) buf[0] = 'n';
        qc->remove = (tolower(buf[0]) == 'y');
        break;
    }
    default:
        break;
    }
}

/* ── transaction helpers ─────────────────────────────────────────────────── */

static alpm_handle_t *alpmInitCallbacks(void)
{
    alpm_handle_t *handle = alpmInit();
    if (!handle) return NULL;
    alpm_option_set_eventcb(handle, onEvent, NULL);
    alpm_option_set_questioncb(handle, onQuestion, NULL);
    return handle;
}

/* Prepare transaction, show what will change, ask y/n. Returns 1 to proceed. */
static int confirmTransaction(alpm_handle_t *handle, const char *verb)
{
    alpm_list_t *data = NULL;
    if (alpm_trans_prepare(handle, &data) != 0)
    {
        fprintf(stderr, RED "Failed to prepare transaction: %s\n" RESET,
                alpm_strerror(alpm_errno(handle)));
        alpm_list_free(data);
        return 0;
    }

    alpm_list_t *add = alpm_trans_get_add(handle);
    alpm_list_t *rem = alpm_trans_get_remove(handle);

    if (!add && !rem)
    {
        printf(GREEN "Nothing to do.\n" RESET);
        return 0;
    }

    if (add)
    {
        printf("\nPackages to %s (%d):\n", verb, (int)alpm_list_count(add));
        for (alpm_list_t *i = add; i; i = alpm_list_next(i))
        {
            alpm_pkg_t *pkg = i->data;
            printf("  %s %s\n", alpm_pkg_get_name(pkg), alpm_pkg_get_version(pkg));
        }
    }
    if (rem)
    {
        printf("\nPackages to remove (%d):\n", (int)alpm_list_count(rem));
        for (alpm_list_t *i = rem; i; i = alpm_list_next(i))
        {
            alpm_pkg_t *pkg = i->data;
            printf("  %s %s\n", alpm_pkg_get_name(pkg), alpm_pkg_get_version(pkg));
        }
    }

    printf("\nProceed? (y/n): ");
    char buf[16] = {'n'};
    if (!fgets(buf, sizeof(buf), stdin)) buf[0] = 'n';
    if (!strchr(buf, '\n')) { int c; while ((c = getchar()) != '\n' && c != EOF); }
    return tolower(buf[0]) == 'y';
}

static void commitOrAbort(alpm_handle_t *handle, const char *verb)
{
    if (confirmTransaction(handle, verb))
    {
        alpm_list_t *data = NULL;
        if (alpm_trans_commit(handle, &data) != 0)
            fprintf(stderr, RED "Transaction failed: %s\n" RESET,
                    alpm_strerror(alpm_errno(handle)));
        alpm_list_free(data);
    }
    alpm_trans_release(handle);
    alpm_release(handle);
}

/* ── read operations (no root required) ─────────────────────────────────── */

void queryPackage(const char *name)
{
    alpm_handle_t *handle = alpmInit();
    if (!handle) return;
    alpm_db_t *localdb = alpm_get_localdb(handle);
    alpm_pkg_t *pkg = alpm_db_get_pkg(localdb, name);
    if (pkg)
        printf(GREEN "%s %s is installed.\n" RESET, name, alpm_pkg_get_version(pkg));
    else
        fprintf(stderr, RED "%s is not installed.\n" RESET, name);
    alpm_release(handle);
}

void searchPackage(const char *query)
{
    alpm_handle_t *handle = alpmInit();
    if (!handle) return;
    alpm_list_t *syncdbs = alpm_get_syncdbs(handle);
    int found = 0;
    for (alpm_list_t *i = syncdbs; i; i = alpm_list_next(i))
    {
        alpm_db_t *db = i->data;
        const char *dbname = alpm_db_get_name(db);
        alpm_list_t *pkgs = alpm_db_get_pkgcache(db);
        for (alpm_list_t *j = pkgs; j; j = alpm_list_next(j))
        {
            alpm_pkg_t *pkg = j->data;
            const char *pname = alpm_pkg_get_name(pkg);
            const char *desc  = alpm_pkg_get_desc(pkg);
            if (strstr(pname, query) || (desc && strstr(desc, query)))
            {
                printf(GREEN "%s/%s" RESET " %s\n    %s\n",
                       dbname, pname, alpm_pkg_get_version(pkg), desc ? desc : "");
                found++;
            }
        }
    }
    if (!found)
        printf(YELLOW "No results for '%s' in official repositories.\n" RESET, query);
    alpm_release(handle);
}

void listAurPackages(const char *query)
{
    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize("/", "/var/lib/pacman", &err);
    if (!handle) { fprintf(stderr, RED "Failed to init: %s\n" RESET, alpm_strerror(err)); return; }

    DIR *dir = opendir("/var/lib/pacman/sync");
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            size_t len = strlen(entry->d_name);
            if (len > 3 && strcmp(entry->d_name + len - 3, ".db") == 0)
            {
                char name[256];
                snprintf(name, sizeof(name), "%.*s", (int)(len - 3), entry->d_name);
                alpm_register_syncdb(handle, name, ALPM_SIG_USE_DEFAULT);
            }
        }
        closedir(dir);
    }

    alpm_list_t *syncdbs = alpm_get_syncdbs(handle);
    alpm_db_t *localdb = alpm_get_localdb(handle);
    alpm_list_t *pkgs = alpm_db_get_pkgcache(localdb);

    int found = 0;
    for (alpm_list_t *i = pkgs; i; i = alpm_list_next(i))
    {
        alpm_pkg_t *pkg = i->data;
        const char *pname = alpm_pkg_get_name(pkg);
        if (alpm_find_dbs_satisfier(handle, syncdbs, pname)) continue;

        if (query && query[0] != '\0')
        {
            if (!strstr(pname, query)) continue;
        }

        printf(GREEN "aur/%s" RESET " %s\n", pname, alpm_pkg_get_version(pkg));
        found++;
    }

    if (!found)
    {
        if (query && query[0] != '\0')
            printf(YELLOW "No AUR packages match '%s'.\n" RESET, query);
        else
            printf(YELLOW "No AUR packages installed.\n" RESET);
    }
    alpm_release(handle);
}

/* ── write operations (require root) ────────────────────────────────────── */

void installPackages(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, RED "Usage: %s install <package1> ...\n" RESET, argv[0]);
        return;
    }

    int numPackages = argc - 2;
    char **packages = argv + 2;

    /* Classify packages — no root needed for reads */
    alpm_handle_t *rh = alpmInit();
    alpm_list_t *sdbs = rh ? alpm_get_syncdbs(rh) : NULL;

    char *repoPkgs[numPackages];
    char *aurPkgs[numPackages];
    int repoCount = 0, aurCount = 0;

    for (int i = 0; i < numPackages; i++)
    {
        int inRepo = rh && sdbs &&
                     alpm_find_dbs_satisfier(rh, sdbs, packages[i]) != NULL;
        if (inRepo)
            repoPkgs[repoCount++] = packages[i];
        else
        {
            printf("'%s' not in official repos, checking AUR...\n", packages[i]);
            if (existingAurPackage(packages[i]))
            {
                printf(YELLOW "Found '%s' in AUR.\n" RESET, packages[i]);
                aurPkgs[aurCount++] = packages[i];
            }
            else
                fprintf(stderr, RED "'%s' not found in any repository.\n" RESET, packages[i]);
        }
    }
    if (rh) alpm_release(rh);

    /* Install repo packages via libalpm — elevate if needed */
    if (repoCount > 0)
    {
        if (geteuid() != 0)
        {
            char **args = malloc((repoCount + 4) * sizeof(char *));
            int ai = 0;
            args[ai++] = "sudo";
            args[ai++] = argv[0];
            args[ai++] = "-S";
            for (int i = 0; i < repoCount; i++) args[ai++] = repoPkgs[i];
            args[ai] = NULL;
            pid_t pid = fork();
            if (pid == 0) { execvp("sudo", args); _exit(1); }
            int status; waitpid(pid, &status, 0);
            free(args);
        }
        else
        {
            alpm_handle_t *handle = alpmInitCallbacks();
            if (handle)
            {
                alpm_trans_init(handle, 0);
                alpm_list_t *syncdbs = alpm_get_syncdbs(handle);
                for (int i = 0; i < repoCount; i++)
                {
                    alpm_pkg_t *pkg = alpm_find_dbs_satisfier(handle, syncdbs, repoPkgs[i]);
                    if (pkg) alpm_add_pkg(handle, pkg);
                    else fprintf(stderr, RED "'%s' not found.\n" RESET, repoPkgs[i]);
                }
                commitOrAbort(handle, "install");
            }
        }
    }

    /* AUR packages — makepkg handles its own elevation */
    if (aurCount > 0)
    {
        char answer[16];
        printf("Install %d AUR package%s (%s) via makepkg? (y/n): ",
               aurCount, aurCount == 1 ? "" : "s", aurPkgs[0]);
        if (!fgets(answer, sizeof(answer), stdin)) answer[0] = 'n';
        if (!strchr(answer, '\n')) { int c; while ((c = getchar()) != '\n' && c != EOF); }
        if (tolower(answer[0]) == 'y' || answer[0] == '\n')
            installAurPackages(aurPkgs, (unsigned int)aurCount);
    }
}

void installPackagesForce(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, RED "Usage: %s install-force <package1> ...\n" RESET, argv[0]); return; }
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    alpm_trans_init(handle, ALPM_TRANS_FLAG_NODEPVERSION | ALPM_TRANS_FLAG_NODEPS);
    alpm_list_t *sdbs = alpm_get_syncdbs(handle);
    for (int i = 2; i < argc; i++)
    {
        alpm_pkg_t *pkg = alpm_find_dbs_satisfier(handle, sdbs, argv[i]);
        if (pkg) alpm_add_pkg(handle, pkg);
        else fprintf(stderr, RED "'%s' not found.\n" RESET, argv[i]);
    }
    commitOrAbort(handle, "force-install");
}

void installLocalPackages(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, RED "Usage: %s install-local <path>\n" RESET, argv[0]); return; }
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    alpm_pkg_t *pkg = NULL;
    if (alpm_pkg_load(handle, argv[2], 1, ALPM_SIG_USE_DEFAULT, &pkg) != 0 || !pkg)
    {
        fprintf(stderr, RED "Failed to load package: %s\n" RESET,
                alpm_strerror(alpm_errno(handle)));
        alpm_release(handle);
        return;
    }
    alpm_trans_init(handle, 0);
    alpm_add_pkg(handle, pkg);
    commitOrAbort(handle, "install");
}

static void alpmRemove(int argc, char *argv[], alpm_transflag_t flags, const char *usage)
{
    if (argc < 3) { fprintf(stderr, RED "Usage: %s %s\n" RESET, argv[0], usage); return; }
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    alpm_trans_init(handle, flags);
    alpm_db_t *localdb = alpm_get_localdb(handle);
    for (int i = 2; i < argc; i++)
    {
        alpm_pkg_t *pkg = alpm_db_get_pkg(localdb, argv[i]);
        if (pkg) alpm_remove_pkg(handle, pkg);
        else fprintf(stderr, RED "'%s' is not installed.\n" RESET, argv[i]);
    }
    commitOrAbort(handle, "remove");
}

void removePackages(int argc, char *argv[])
{
    alpmRemove(argc, argv, 0, "remove <package1> ...");
}

void removePackagesWithDependencies(int argc, char *argv[])
{
    alpmRemove(argc, argv, ALPM_TRANS_FLAG_RECURSE, "remove-dep <package1> ...");
}

void removePackagesForce(int argc, char *argv[])
{
    alpmRemove(argc, argv, ALPM_TRANS_FLAG_NODEPS, "remove-force <package1> ...");
}

void removePackagesForceWithDependencies(int argc, char *argv[])
{
    alpmRemove(argc, argv,
               ALPM_TRANS_FLAG_NODEPS | ALPM_TRANS_FLAG_RECURSE,
               "remove-force-dep <package1> ...");
}

void removeOrphanPackages(void)
{
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    alpm_db_t *localdb = alpm_get_localdb(handle);
    alpm_list_t *pkgs = alpm_db_get_pkgcache(localdb);
    alpm_trans_init(handle, ALPM_TRANS_FLAG_RECURSE | ALPM_TRANS_FLAG_NOSAVE);
    int found = 0;
    for (alpm_list_t *i = pkgs; i; i = alpm_list_next(i))
    {
        alpm_pkg_t *pkg = i->data;
        if (alpm_pkg_get_reason(pkg) != ALPM_PKG_REASON_DEPEND) continue;
        alpm_list_t *req = alpm_pkg_compute_requiredby(pkg);
        if (!req) { alpm_remove_pkg(handle, pkg); found++; }
        alpm_list_free_inner(req, free);
        alpm_list_free(req);
    }
    if (!found)
    {
        printf(GREEN "No orphan packages found.\n" RESET);
        alpm_trans_release(handle);
        alpm_release(handle);
        return;
    }
    commitOrAbort(handle, "remove (orphans)");
}

void updateSystem(void)
{
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    printf("Syncing databases...\n");
    alpm_list_t *syncdbs = alpm_get_syncdbs(handle);
    alpm_db_update(handle, syncdbs, 0);
    alpm_trans_init(handle, 0);
    alpm_sync_sysupgrade(handle, 0);
    commitOrAbort(handle, "upgrade");
}

void refreshRepo(void)
{
    alpm_handle_t *handle = alpmInitCallbacks();
    if (!handle) return;
    printf("Syncing package databases...\n");
    alpm_list_t *syncdbs = alpm_get_syncdbs(handle);
    if (alpm_db_update(handle, syncdbs, 0) >= 0)
        printf(GREEN "Databases synced.\n" RESET);
    else
        fprintf(stderr, RED "Sync failed: %s\n" RESET, alpm_strerror(alpm_errno(handle)));
    alpm_release(handle);
}

void fullUpdate(char *selfPath)
{
    printf(GREEN "::" RESET " Updating system packages...\n\n");
    if (geteuid() != 0)
    {
        pid_t pid = fork();
        if (pid == 0) { execlp("sudo", "sudo", selfPath, "update", NULL); _exit(1); }
        int status; waitpid(pid, &status, 0);
    }
    else
        updateSystem();
    printf("\n" GREEN "::" RESET " Checking AUR packages for updates...\n\n");
    upgradeAurPackages();
}

void modifyRepo(void)
{
    char editor[256] = "vi";
    char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "Could not get home directory\n"); exit(1); }

    char configPath[256];
    snprintf(configPath, sizeof(configPath), "%s/.aurcrc", home);
    FILE *file = fopen(configPath, "r");
    if (file)
    {
        if (fscanf(file, "editor=%255s", editor) != 1)
            fprintf(stderr, "Failed to read editor from config\n");
        fclose(file);
    }

    const size_t commandSize = 1024;
    char command[commandSize];
    if (snprintf(command, commandSize, "sudo %s /etc/pacman.d/mirrorlist", editor) >= (int)commandSize)
    {
        fprintf(stderr, "Failed to build command\n");
        return;
    }
    executeCommandWithUserShell(command);
}

void selfUpdate(void)
{
    char tmpDir[] = "/tmp/aurc-update-XXXXXX";
    char rmCmd[128], installCmd[256], buildCmd[256];
    int status;
    pid_t pid;

    if (!mkdtemp(tmpDir)) { perror("mkdtemp"); return; }

    printf("Fetching latest aurc...\n");
    pid = fork();
    if (pid == -1) { perror("fork"); goto cleanup; }
    if (pid == 0)
    {
        execlp("git", "git", "clone", "--depth=1",
               "https://github.com/resslr/aurc.git", tmpDir, NULL);
        _exit(1);
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, RED "Failed to fetch latest aurc.\n" RESET);
        goto cleanup;
    }

    printf("Building...\n");
    snprintf(buildCmd, sizeof(buildCmd), "cd %s/src && make build", tmpDir);
    pid = fork();
    if (pid == -1) { perror("fork"); goto cleanup; }
    if (pid == 0) { execlp("sh", "sh", "-c", buildCmd, NULL); _exit(1); }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, RED "Build failed.\n" RESET);
        goto cleanup;
    }

    printf("Installing...\n");
    snprintf(installCmd, sizeof(installCmd), "cd %s/src && sudo make install", tmpDir);
    pid = fork();
    if (pid == -1) { perror("fork"); goto cleanup; }
    if (pid == 0) { execlp("sh", "sh", "-c", installCmd, NULL); _exit(1); }
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        printf(GREEN "aurc updated successfully.\n" RESET);
    else
        fprintf(stderr, RED "Install failed.\n" RESET);

cleanup:
    snprintf(rmCmd, sizeof(rmCmd), "rm -rf %s", tmpDir);
    (void)system(rmCmd);
}
