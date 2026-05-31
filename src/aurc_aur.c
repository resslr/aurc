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
        system(extractCommand);

        char userInput[16];
        printf("View PKGBUILD for '%s' before installation? (Recommended) (y/n): ", packageName);
        fgets(userInput, sizeof(userInput), stdin);
        drainStdin(userInput);
        if (userInput[0] == '\n')
            userInput[0] = 'y';

        if (tolower(userInput[0]) == 'y')
        {
            displayPkgBuild(packageName, downloadDir);

            printf("Continue with installation of '%s'? (y/n): ", packageName);
            fgets(userInput, sizeof(userInput), stdin);
            drainStdin(userInput);
            if (userInput[0] == '\n')
                userInput[0] = 'y';

            if (tolower(userInput[0]) != 'y')
            {
                fprintf(stderr, "Installation of '%s' aborted.\n", packageName);
                char cleanupCommand[600];
                snprintf(cleanupCommand, sizeof(cleanupCommand), "rm -rf %s%s %s", downloadDir, packageName, tarPath);
                system(cleanupCommand);
                continue;
            }
        }

        char buildCommand[600];
        snprintf(buildCommand, sizeof(buildCommand), "cd %s%s && makepkg -si", downloadDir, packageName);

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

void displayPkgBuild(const char *packageName, const char *downloadDir)
{
    char displayCommand[600];
    snprintf(displayCommand, sizeof(displayCommand), "less %s%s/PKGBUILD", downloadDir, packageName);
    system(displayCommand);
}

void clearAurBuildCache()
{
    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, "Could not get home directory\n");
        return;
    }

    char cleanupCommand[512];
    snprintf(cleanupCommand, sizeof(cleanupCommand), "rm -rf %s/.cache/aurc/*", home);
    printf(YELLOW "Clearing AUR build cache...\n" RESET);
    system(cleanupCommand);
    printf(GREEN "Done.\n" RESET);
}
