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

        char userInput[4];
        printf("View PKGBUILD for '%s' before installation? (Recommended) (y/n): ", packageName);
        fgets(userInput, sizeof(userInput), stdin);
        if (userInput[0] == '\n')
            userInput[0] = 'y';

        if (tolower(userInput[0]) == 'y')
        {
            displayPkgBuild(packageName, downloadDir);

            printf("Continue with installation of '%s'? (y/n): ", packageName);
            fgets(userInput, sizeof(userInput), stdin);
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

void queryAurRepo(const char *packageName, const char *message)
{
    char url[500];
    int ret = snprintf(url, sizeof(url), "https://aur.archlinux.org/rpc/?v=5&type=search&arg=%s", packageName);
    if (ret < 0 || ret >= (int)sizeof(url))
    {
        fprintf(stderr, RED "URL too long.\n" RESET);
        return;
    }

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
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
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

    struct json_object *results;
    if (!json_object_object_get_ex(parsed, "results", &results))
    {
        printf(YELLOW "No AUR packages found for '%s'.\n" RESET, packageName);
        json_object_put(parsed);
        return;
    }

    int count = json_object_array_length(results);
    if (count == 0)
    {
        printf(YELLOW "No AUR packages found for '%s'.\n" RESET, packageName);
        json_object_put(parsed);
        return;
    }

    printf(GREEN "%s '%s'" RESET " (%d result%s):\n\n", message, packageName, count, count == 1 ? "" : "s");

    for (int i = 0; i < count; i++)
    {
        struct json_object *pkg = json_object_array_get_idx(results, i);
        struct json_object *name_obj, *desc_obj, *ver_obj;

        json_object_object_get_ex(pkg, "Name", &name_obj);
        json_object_object_get_ex(pkg, "Description", &desc_obj);
        json_object_object_get_ex(pkg, "Version", &ver_obj);

        const char *name = name_obj ? json_object_get_string(name_obj) : "unknown";
        const char *desc = desc_obj ? json_object_get_string(desc_obj) : "No description";
        const char *ver = ver_obj ? json_object_get_string(ver_obj) : "unknown";

        printf(GREEN "aur/%s" RESET " %s\n    %s\n", name, ver, desc);
    }

    json_object_put(parsed);
}

void searchAurPackage(char *packageName)
{
    queryAurRepo(packageName, "Search results for");
}

void existingPackage(const char *packageName)
{
    queryAurRepo(packageName, "Available & similar packages for");
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
