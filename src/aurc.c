#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <search.h>
#include "aurc_aur.c"
#include "aurc_shell.c"
#include "aurc_pac.c"
#include "aurc_valid.c"
#include "commands.h"
#include "constants.h"

void sanitizeInput(const char *input, char *output, size_t size)
{
    if (strnlen(input, size) >= size)
    {
        fprintf(stderr, "Too many args.\n");
        exit(1);
    }
    strncpy(output, input, size - 1);
    output[size - 1] = '\0';
}

signed int main(unsigned int argc, char *argv[])
{
    // Check for help flag
    if (argc == 2 && getCommandType(argv[1]) == CMD_HELP)
    {
        printf("Aurc Package Manager - Help\n\n");
        printf("Usage: %s [action] [package(s)]\n", argv[0]);
        printf("       %s (no args) — full system + AUR upgrade\n\n", argv[0]);
        printf("%-32s %s\n", "Action", "Short");
        printf("%-32s %s\n", "──────────────────────────────", "─────");
        printf(GREEN "  %-30s" RESET " %s\n", "update",          "-Syu");
        printf(GREEN "  %-30s" RESET " %s\n", "self-update",     "-Su");
        printf(GREEN "  %-30s" RESET " %s\n", "refresh",         "-Syy");
        printf(GREEN "  %-30s" RESET " %s\n", "install",         "-S");
        printf(GREEN "  %-30s" RESET " %s\n", "install-aur",     "-Sa");
        printf(GREEN "  %-30s" RESET " %s\n", "install-local",   "-Sl");
        printf(GREEN "  %-30s" RESET " %s\n", "config",          "");
        printf(GREEN "  %-30s" RESET " %s\n", "github",          "");
        printf(YELLOW "  %-30s" RESET " %s\n", "install-force",  "-Sf");
        printf(YELLOW "  %-30s" RESET " %s\n", "modify-repo",    "-Me");
        printf(YELLOW "  %-30s" RESET " %s\n", "query",          "-Q");
        printf(YELLOW "  %-30s" RESET " %s\n", "search",         "-Ss");
        printf(YELLOW "  %-30s" RESET " %s\n", "search-aur [-s]","-Ssa");
        printf(YELLOW "  %-30s" RESET " %s\n", "clear-aur-cache","-C");
        printf(RED    "  %-30s" RESET " %s\n", "remove",         "-R");
        printf(RED    "  %-30s" RESET " %s\n", "remove-dep",     "-Rs");
        printf(RED    "  %-30s" RESET " %s\n", "remove-force",   "-Rdd");
        printf(RED    "  %-30s" RESET " %s\n", "remove-force-dep","-Rdds");
        printf(RED    "  %-30s" RESET " %s\n", "remove-orp",     "-Ro");
        printf("\nOptions:\n");
        printf("  --version, -v    Show version\n");
        printf("  --help, -h       Show this help\n");
        return 0;
    }
    if (argc == 2 && getCommandType(argv[1]) == CMD_VERSION)
    {
        printf("\n");
        printf("Aurc Package Manager Version %s\n", VERSION);
        printf("Copyright (C) 2023-2026 Resslr\n");
        printf("\n");
        printf("This program may be freely redistributed under\n");
        printf("the terms of the GNU General Public License.\n");
        printf("\n");
        return 0;
    }

    if (argc < 2)
    {
        fullUpdate();
        return 0;
    }

    const char *mapped = getShorthandAction(argv[1]);
    char *action = mapped ? (char *)mapped : argv[1];

    if (!isValidAction(action))
    {
        fprintf(stderr, RED "Invalid action: %s\n" RESET, action);
        return 1;
    }

    if (strcmp(action, "install") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            installPackages(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s install <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "install-local") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            installLocalPackages(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s install-local <local_package_path>\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "install-aur") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            int numPackages = argc - 2;
            char **packageNames = argv + 2;
            installAurPackages(packageNames, numPackages);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s install-aur <package_name1> <package_name2> ... <package_nameN>\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "github") == 0)
    {
        printf("Opening GitHub...\n");
        char sanitizedCommand[MAX_INPUT_SIZE];
        snprintf(sanitizedCommand, sizeof(sanitizedCommand), "xdg-open %s >/dev/null 2>&1", "https://github.com/resslr/aurc");
        int result = system(sanitizedCommand);
        if (result == 0)
        {
            printf(GREEN "GitHub opened successfully!\n" RESET);
            return 0;
        }
        else
        {
            fprintf(stderr, RED "Error: Failed to open GitHub!\n" RESET);
            return 1;
        }
    }
    else if (strcmp(action, "search-aur") == 0)
    {
        if (argc == 3)
        {
            searchAurPackage(argv[2], 0);
        }
        else if (argc == 4 && strcmp(argv[2], "-s") == 0)
        {
            searchAurPackage(argv[3], 1);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s search-aur [-s] <package_name>\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "install-force") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            installPackagesForce(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s install-force <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "remove") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            removePackages(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s remove <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "query") == 0)
    {
        if (argc == 3)
        {
            queryPackage(argv[2]);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s query <package_name>\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "search") == 0)
    {
        if (argc == 3)
        {
            searchPackage(argv[2]);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s search <package_name>\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "remove-dep") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            removePackagesWithDependencies(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s remove-dep <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "remove-force") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            removePackagesForce(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s remove-force <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "remove-force-dep") == 0)
    {
        if (MULTIPLE_ARGS)
        {
            removePackagesForceWithDependencies(argc, argv);
        }
        else
        {
            fprintf(stderr, RED "Usage: %s remove-force-dep <package_name1> <package_name2> ...\n" RESET, argv[0]);
            return 1;
        }
    }
    else if (strcmp(action, "remove-orp") == 0)
    {
        removeOrphanPackages();
        return 0;
    }
    else if (strcmp(action, "update") == 0)
    {
        updateSystem();
        return 0;
    }
    else if (strcmp(action, "refresh") == 0)
    {
        refreshRepo();
        return 0;
    }
    else if (strcmp(action, "modify-repo") == 0)
    {
        modifyRepo();
        return 0;
    }
    else if (strcmp(action, "clear-aur-cache") == 0)
    {
        clearAurBuildCache();
        return 0;
    }
    else if (strcmp(action, "self-update") == 0)
    {
        selfUpdate();
        return 0;
    }
    else if (strcmp(action, "config") == 0)
    {
        handleConfigCommand(argc, argv);
        return 0;
    }

    return 0;
}
