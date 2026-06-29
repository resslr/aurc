#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <regex.h>
#include <dirent.h>
#include <alpm.h>
#include <alpm_list.h>
#include <json-c/json.h>
#include "colors.h"

void displayPkgBuild(const char *packageName, const char *downloadDir);

static void drainStdin(const char *buf)
{
    if (!strchr(buf, '\n'))
    {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
}

typedef struct
{
    char *data;
    size_t size;
} CurlBuffer;

static size_t growingWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    CurlBuffer *buf = (CurlBuffer *)userp;

    char *newData = realloc(buf->data, buf->size + totalSize + 1);
    if (!newData)
        return 0;

    buf->data = newData;
    memcpy(buf->data + buf->size, contents, totalSize);
    buf->size += totalSize;
    buf->data[buf->size] = '\0';

    return totalSize;
}

int existingAurPackage(const char *packageName)
{
    CURL *curl;
    CURLcode res;
    CurlBuffer buf = {NULL, 0};
    char url[512];

    snprintf(url, sizeof(url), "https://aur.archlinux.org/rpc/?v=5&type=info&arg=%s", packageName);

    curl = curl_easy_init();
    if (!curl)
        return 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growingWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(buf.data);
        return 0;
    }

    struct json_object *parsed_json = json_tokener_parse(buf.data);
    free(buf.data);

    if (!parsed_json)
        return 0;

    struct json_object *resultcount;
    json_object_object_get_ex(parsed_json, "resultcount", &resultcount);
    int count = json_object_get_int(resultcount);
    json_object_put(parsed_json);

    return count > 0;
}

void installAurPackages(char **packageNames, unsigned int numPackages)
{
    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, "Could not get home directory\n");
        exit(1);
    }

    regex_t regex;
    int reti = regcomp(&regex, "^[a-zA-Z0-9@._+-]+$", REG_EXTENDED);
    if (reti)
    {
        fprintf(stderr, "Failed to compile regex\n");
        exit(1);
    }

    char downloadDir[256];
    snprintf(downloadDir, sizeof(downloadDir), "%s/.cache/aurc/", home);
    mkdir(downloadDir, 0755);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Failed to initialize curl\n");
        regfree(&regex);
        exit(1);
    }

    printf("Package(s) requested:");
    for (unsigned int i = 0; i < numPackages; i++)
        printf(" %s", packageNames[i]);
    printf("\n");

    for (unsigned int i = 0; i < numPackages; i++)
    {
        char *packageName = packageNames[i];

        reti = regexec(&regex, packageName, 0, NULL, 0);
        if (reti == REG_NOMATCH)
        {
            printf(YELLOW "Invalid package name '%s'.\n" RESET, packageName);
            continue;
        }
        else if (reti != 0)
        {
            char msgbuf[100];
            regerror(reti, &regex, msgbuf, sizeof(msgbuf));
            fprintf(stderr, "Regex match failed: %s\n", msgbuf);
            continue;
        }

        if (!existingAurPackage(packageName))
        {
            fprintf(stderr, RED "Package '%s' does not exist in AUR.\n" RESET, packageName);
            continue;
        }

        char url[512];
        snprintf(url, sizeof(url), "https://aur.archlinux.org/cgit/aur.git/snapshot/%s.tar.gz", packageName);

        char tarPath[512];
        snprintf(tarPath, sizeof(tarPath), "%s%s.tar.gz", downloadDir, packageName);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        FILE *file = fopen(tarPath, "wb");
        if (!file)
        {
            fprintf(stderr, "Failed to open file %s\n", tarPath);
            continue;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        CURLcode res = curl_easy_perform(curl);
        fclose(file);

        if (res != CURLE_OK)
        {
            fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
            continue;
        }

        char extractCommand[600];
        snprintf(extractCommand, sizeof(extractCommand), "tar -xzf %s -C %s", tarPath, downloadDir);
        if (system(extractCommand) != 0)
        {
            fprintf(stderr, RED "Failed to extract '%s'.\n" RESET, packageName);
            continue;
        }

        char userInput[16];
        printf("View PKGBUILD for '%s' before installation? (Recommended) (y/n): ", packageName);
        if (!fgets(userInput, sizeof(userInput), stdin)) userInput[0] = 'n';
        drainStdin(userInput);
        if (userInput[0] == '\n')
            userInput[0] = 'y';

        if (tolower(userInput[0]) == 'y')
        {
            displayPkgBuild(packageName, downloadDir);

            printf("Continue with installation of '%s'? (y/n): ", packageName);
            if (!fgets(userInput, sizeof(userInput), stdin)) userInput[0] = 'n';
            drainStdin(userInput);
            if (userInput[0] == '\n')
                userInput[0] = 'y';

            if (tolower(userInput[0]) != 'y')
            {
                fprintf(stderr, "Installation of '%s' aborted.\n", packageName);
                char cleanupCommand[600];
                snprintf(cleanupCommand, sizeof(cleanupCommand), "rm -rf %s%s %s", downloadDir, packageName, tarPath);
                (void)system(cleanupCommand);
                continue;
            }
        }

        /* --pkg restricts a split PKGBUILD to just the requested package */
        char buildCommand[700];
        snprintf(buildCommand, sizeof(buildCommand), "cd %s%s && makepkg -si --pkg=%s",
                 downloadDir, packageName, packageName);

        pid_t pid = fork();
        if (pid == -1)
        {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            execlp("sh", "sh", "-c", buildCommand, (char *)NULL);
            _exit(EXIT_FAILURE);
        }
        else
        {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                printf(RED "Installation of '%s' failed.\n" RESET, packageName);
            else
                printf(GREEN "Installation of '%s' complete.\n" RESET, packageName);
        }

        char cleanupCmd[700];
        snprintf(cleanupCmd, sizeof(cleanupCmd), "rm -rf %s%s %s", downloadDir, packageName, tarPath);
        (void)system(cleanupCmd);
    }

    regfree(&regex);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

/* ── AUR search ─────────────────────────────────────────────────────────── */

typedef struct
{
    char *name;
    char *version;
    char *description;
    int numVotes;
    double popularity;
    int score;
} AurPackage;

static int scorePackage(const AurPackage *pkg, const char *query)
{
    int score = 0;
    if (strcmp(pkg->name, query) == 0)
        score += 1000;
    else if (strncasecmp(pkg->name, query, strlen(query)) == 0)
        score += 100;
    score += (int)(pkg->popularity * 10.0);
    score += pkg->numVotes / 10;
    return score;
}

static int comparePackages(const void *a, const void *b)
{
    return ((const AurPackage *)b)->score - ((const AurPackage *)a)->score;
}

static int displayPackages(const AurPackage *pkgs, int count, const char *filter)
{
    int shown = 0;
    for (int i = 0; i < count; i++)
    {
        if (filter && filter[0] != '\0' &&
            !strstr(pkgs[i].name, filter) &&
            !(pkgs[i].description && strstr(pkgs[i].description, filter)))
            continue;
        printf(GREEN "aur/%s" RESET " %s\n    %s\n",
               pkgs[i].name, pkgs[i].version, pkgs[i].description);
        shown++;
    }
    return shown;
}

static void freePackages(AurPackage *pkgs, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(pkgs[i].name);
        free(pkgs[i].version);
        free(pkgs[i].description);
    }
    free(pkgs);
}

void searchAurPackage(const char *query, int specific)
{
    char url[500];
    snprintf(url, sizeof(url), "https://aur.archlinux.org/rpc/?v=5&type=search&arg=%s", query);

    CurlBuffer buf = {NULL, 0};
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growingWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(res));
            free(buf.data);
            curl_global_cleanup();
            return;
        }
    }
    curl_global_cleanup();

    if (!buf.data)
    {
        fprintf(stderr, RED "No data received from AUR.\n" RESET);
        return;
    }

    struct json_object *parsed = json_tokener_parse(buf.data);
    free(buf.data);

    if (!parsed)
    {
        fprintf(stderr, RED "Failed to parse AUR response.\n" RESET);
        return;
    }

    struct json_object *results_obj;
    if (!json_object_object_get_ex(parsed, "results", &results_obj) ||
        json_object_array_length(results_obj) == 0)
    {
        printf(YELLOW "No AUR packages found for '%s'.\n" RESET, query);
        json_object_put(parsed);
        return;
    }

    int total = json_object_array_length(results_obj);
    AurPackage *pkgs = calloc(total, sizeof(AurPackage));
    if (!pkgs)
    {
        json_object_put(parsed);
        return;
    }

    int count = 0;
    for (int i = 0; i < total; i++)
    {
        struct json_object *pkg = json_object_array_get_idx(results_obj, i);
        struct json_object *o;

        json_object_object_get_ex(pkg, "Name", &o);
        const char *name = o ? json_object_get_string(o) : NULL;
        if (!name)
            continue;

        pkgs[count].name = strdup(name);

        json_object_object_get_ex(pkg, "Version", &o);
        pkgs[count].version = strdup(o ? json_object_get_string(o) : "unknown");

        json_object_object_get_ex(pkg, "Description", &o);
        const char *desc = (o && json_object_get_string(o)) ? json_object_get_string(o) : "No description";
        pkgs[count].description = strdup(desc);

        json_object_object_get_ex(pkg, "NumVotes", &o);
        pkgs[count].numVotes = o ? json_object_get_int(o) : 0;

        json_object_object_get_ex(pkg, "Popularity", &o);
        pkgs[count].popularity = o ? json_object_get_double(o) : 0.0;

        pkgs[count].score = scorePackage(&pkgs[count], query);
        count++;
    }

    json_object_put(parsed);
    qsort(pkgs, count, sizeof(AurPackage), comparePackages);

    if (specific)
    {
        int found = 0;
        for (int i = 0; i < count; i++)
        {
            if (strcmp(pkgs[i].name, query) == 0)
            {
                printf(GREEN "aur/%s" RESET " %s\n    %s\n",
                       pkgs[i].name, pkgs[i].version, pkgs[i].description);
                found = 1;
                break;
            }
        }
        if (!found)
            printf(YELLOW "No AUR package named exactly '%s'.\n" RESET, query);
    }
    else
    {
        printf(GREEN "Search results for '%s'" RESET " (%d result%s, sorted by relevance):\n\n",
               query, count, count == 1 ? "" : "s");
        displayPackages(pkgs, count, NULL);

        char filter[128];
        while (1)
        {
            printf("\n" YELLOW "Filter (empty to exit): " RESET);
            fflush(stdout);
            if (!fgets(filter, sizeof(filter), stdin))
                break;
            drainStdin(filter);
            filter[strcspn(filter, "\n")] = '\0';
            if (filter[0] == '\0')
                break;
            printf("\n");
            if (displayPackages(pkgs, count, filter) == 0)
                printf(YELLOW "No results match '%s'.\n" RESET, filter);
        }
    }

    freePackages(pkgs, count);
}

/* ── AUR upgrade ─────────────────────────────────────────────────────────── */

typedef struct
{
    char name[256];
    char version[128];
} InstalledPkg;

static int vercmpResult(const char *v1, const char *v2)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
        return 0;

    pid_t pid = fork();
    if (pid == 0)
    {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("vercmp", "vercmp", v1, v2, NULL);
        _exit(1);
    }
    close(pipefd[1]);

    char buf[32] = {0};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    if (n > 0) buf[n] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    return atoi(buf);
}

static InstalledPkg *getForeignPackages(int *count)
{
    *count = 0;

    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize("/", "/var/lib/pacman", &err);
    if (!handle)
        return NULL;

    /* Register sync DBs so we can detect which packages are foreign */
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
    alpm_list_t *allpkgs = alpm_db_get_pkgcache(localdb);

    int total = (int)alpm_list_count(allpkgs);
    InstalledPkg *pkgs = calloc(total, sizeof(InstalledPkg));
    if (!pkgs) { alpm_release(handle); return NULL; }

    int idx = 0;
    for (alpm_list_t *i = allpkgs; i; i = alpm_list_next(i))
    {
        alpm_pkg_t *pkg = i->data;
        const char *pname = alpm_pkg_get_name(pkg);
        if (!alpm_find_dbs_satisfier(handle, syncdbs, pname))
        {
            strncpy(pkgs[idx].name, pname, sizeof(pkgs[idx].name) - 1);
            strncpy(pkgs[idx].version, alpm_pkg_get_version(pkg),
                    sizeof(pkgs[idx].version) - 1);
            idx++;
        }
    }

    alpm_release(handle);
    *count = idx;
    return pkgs;
}

void upgradeAurPackages()
{
    int installedCount = 0;
    InstalledPkg *installed = getForeignPackages(&installedCount);

    if (installedCount == 0)
    {
        printf("No foreign/AUR packages installed.\n");
        free(installed);
        return;
    }

    printf("Checking %d AUR package%s for updates...\n",
           installedCount, installedCount == 1 ? "" : "s");

    size_t urlCap = 64;
    for (int i = 0; i < installedCount; i++)
        urlCap += strlen(installed[i].name) + 8;

    char *url = malloc(urlCap);
    if (!url)
    {
        free(installed);
        return;
    }

    int w = snprintf(url, urlCap, "https://aur.archlinux.org/rpc/?v=5&type=info");
    for (int i = 0; i < installedCount; i++)
        w += snprintf(url + w, urlCap - (size_t)w, "&arg[]=%s", installed[i].name);

    CurlBuffer buf = {NULL, 0};
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growingWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, RED "AUR query failed: %s\n" RESET, curl_easy_strerror(res));
            free(buf.data);
            free(url);
            free(installed);
            curl_global_cleanup();
            return;
        }
    }
    curl_global_cleanup();
    free(url);

    if (!buf.data)
    {
        free(installed);
        return;
    }

    struct json_object *parsed = json_tokener_parse(buf.data);
    free(buf.data);
    if (!parsed)
    {
        free(installed);
        return;
    }

    struct json_object *results_obj;
    if (!json_object_object_get_ex(parsed, "results", &results_obj))
    {
        json_object_put(parsed);
        free(installed);
        return;
    }

    int resultCount = json_object_array_length(results_obj);
    char **toUpdate = malloc((size_t)installedCount * sizeof(char *));
    int updateCount = 0;

    for (int i = 0; i < resultCount; i++)
    {
        struct json_object *pkg = json_object_array_get_idx(results_obj, i);
        struct json_object *name_obj, *ver_obj;

        json_object_object_get_ex(pkg, "Name", &name_obj);
        json_object_object_get_ex(pkg, "Version", &ver_obj);
        if (!name_obj || !ver_obj)
            continue;

        const char *aurName = json_object_get_string(name_obj);
        const char *aurVer = json_object_get_string(ver_obj);

        for (int j = 0; j < installedCount; j++)
        {
            if (strcmp(installed[j].name, aurName) == 0)
            {
                if (vercmpResult(installed[j].version, aurVer) < 0)
                {
                    if (updateCount == 0)
                        printf("\n" BOLD "AUR packages to upgrade:" RESET "\n\n");
                    printf("  %2d  " MAGENTA "aur/" RESET BOLD "%-25s" RESET GRAY "%s" RESET " -> " GREEN "%s" RESET "\n",
                           updateCount + 1, aurName, installed[j].version, aurVer);
                    toUpdate[updateCount++] = strdup(aurName);
                }
                break;
            }
        }
    }

    json_object_put(parsed);
    free(installed);

    if (updateCount == 0)
        printf(GREEN "All AUR packages are up to date.\n" RESET);
    else
    {
        printf("\n");
        installAurPackages(toUpdate, (unsigned int)updateCount);
    }

    for (int i = 0; i < updateCount; i++)
        free(toUpdate[i]);
    free(toUpdate);
}

int warnAurOrphanedPackages(void)
{
    int installedCount = 0;
    InstalledPkg *installed = getForeignPackages(&installedCount);
    if (installedCount == 0 || !installed)
    {
        free(installed);
        return 0;
    }

    size_t urlCap = 64;
    for (int i = 0; i < installedCount; i++)
        urlCap += strlen(installed[i].name) + 8;

    char *url = malloc(urlCap);
    if (!url) { free(installed); return 0; }

    int w = snprintf(url, urlCap, "https://aur.archlinux.org/rpc/?v=5&type=info");
    for (int i = 0; i < installedCount; i++)
        w += snprintf(url + w, urlCap - (size_t)w, "&arg[]=%s", installed[i].name);

    CurlBuffer buf = {NULL, 0};
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growingWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK)
        {
            free(buf.data);
            free(url);
            free(installed);
            curl_global_cleanup();
            return 0;
        }
    }
    curl_global_cleanup();
    free(url);

    if (!buf.data) { free(installed); return 0; }

    struct json_object *parsed = json_tokener_parse(buf.data);
    free(buf.data);
    if (!parsed) { free(installed); return 0; }

    struct json_object *results_obj;
    if (!json_object_object_get_ex(parsed, "results", &results_obj))
    {
        json_object_put(parsed);
        free(installed);
        return 0;
    }

    int resultCount = json_object_array_length(results_obj);
    int orphanCount = 0;

    for (int i = 0; i < resultCount; i++)
    {
        struct json_object *pkg = json_object_array_get_idx(results_obj, i);
        struct json_object *name_obj, *maint_obj;

        json_object_object_get_ex(pkg, "Name", &name_obj);
        if (!name_obj) continue;

        if (!json_object_object_get_ex(pkg, "Maintainer", &maint_obj) ||
            (maint_obj && json_object_get_type(maint_obj) != json_type_null))
            continue;

        if (orphanCount == 0)
            printf(YELLOW "\n:: Installed AUR packages with no maintainer (orphaned on AUR):\n" RESET);
        printf("   %s\n", json_object_get_string(name_obj));
        orphanCount++;
    }

    json_object_put(parsed);
    free(installed);
    return orphanCount;
}

void displayPkgBuild(const char *packageName, const char *downloadDir)
{
    char displayCommand[600];
    snprintf(displayCommand, sizeof(displayCommand), "less %s%s/PKGBUILD", downloadDir, packageName);
    (void)system(displayCommand);
}

void clearAurBuildCache()
{
    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, "Could not get home directory\n");
        return;
    }

    char cacheDir[512];
    snprintf(cacheDir, sizeof(cacheDir), "%s/.cache/aurc", home);

    char sizeCmd[600];
    snprintf(sizeCmd, sizeof(sizeCmd), "du -sh %s 2>/dev/null | cut -f1", cacheDir);
    FILE *fp = popen(sizeCmd, "r");
    char sizeBuf[32] = "0";
    if (fp)
    {
        if (!fgets(sizeBuf, sizeof(sizeBuf), fp)) sizeBuf[0] = '\0';
        pclose(fp);
        size_t len = strlen(sizeBuf);
        if (len > 0 && sizeBuf[len - 1] == '\n') sizeBuf[len - 1] = '\0';
    }

    char cleanupCommand[600];
    snprintf(cleanupCommand, sizeof(cleanupCommand), "rm -rf %s/*", cacheDir);
    printf(YELLOW "Clearing AUR build cache (%s)...\n" RESET, sizeBuf[0] ? sizeBuf : "0");
    (void)system(cleanupCommand);
    printf(GREEN "Cache cleared.\n" RESET);
}
