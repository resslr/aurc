// commands.h

#ifndef COMMANDS_H
#define COMMANDS_H

#include <string.h>
#include <string.h>
#include "colors.h"

// Define an enum for the command types
typedef enum
{
    CMD_VERSION,
    CMD_HELP,
    CMD_UNKNOWN
} CommandType;

// Function to map strings to CommandType values
CommandType getCommandType(const char *str)
{
    if (strncmp(str, "--version", 9) == 0 || strncmp(str, "-v", 2) == 0)
    {
        return CMD_VERSION;
    }
    if (strncmp(str, "--help", 6) == 0 || strncmp(str, "-h", 2) == 0)
    {
        return CMD_HELP;
    }
    return CMD_UNKNOWN;
}

const char *getShorthandAction(const char *flag)
{
    if (strcmp(flag, "-S")    == 0) return "install";
    if (strcmp(flag, "-Sa")   == 0) return "install-aur";
    if (strcmp(flag, "-Sl")   == 0) return "install-local";
    if (strcmp(flag, "-Sf")   == 0) return "install-force";
    if (strcmp(flag, "-R")    == 0) return "remove";
    if (strcmp(flag, "-Rs")   == 0) return "remove-dep";
    if (strcmp(flag, "-Rdd")  == 0) return "remove-force";
    if (strcmp(flag, "-Rdds") == 0) return "remove-force-dep";
    if (strcmp(flag, "-Ro")   == 0) return "remove-orp";
    if (strcmp(flag, "-Ss")   == 0) return "search";
    if (strcmp(flag, "-Ssa")  == 0) return "search-aur";
    if (strcmp(flag, "-Q")    == 0) return "query";
    if (strcmp(flag, "-Syu")  == 0) return "update";
    if (strcmp(flag, "-Su")   == 0) return "self-update";
    if (strcmp(flag, "-Syy")  == 0) return "refresh";
    if (strcmp(flag, "-C")    == 0) return "clear-aur-cache";
    if (strcmp(flag, "-Me")   == 0) return "modify-repo";
    return NULL;
}

void handleConfigCommand(int argc, char *argv[])
{
    // Error handling
    if (argc < 3 || (strncmp(argv[2], "--editor", 8) != 0 && strncmp(argv[2], "-e", 2) != 0))
    {
        fprintf(stderr, RED "Usage: %s config -e <editor>\n" RESET, argv[0]);
        exit(1);
    }

    if (argc == 3)
    {
        fprintf(stderr, RED "Argument required (Ex: %s config -e vim)\n" RESET, argv[0]);
        exit(1);
    }

    char *editor = argv[3];
    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, RED "Could not get home directory\n" RESET);
        exit(1);
    }

    char configPath[256];
    snprintf(configPath, sizeof(configPath), "%s/.aurcrc", home);

    FILE *file = fopen(configPath, "w");
    if (!file)
    {
        fprintf(stderr, RED "Failed to open configuration file\n" RESET);
        exit(1);
    }

    // Main logic
    fprintf(file, "editor=%s\n", editor);
    fclose(file);

    printf(GREEN "Successfully set editor to %s\n" RESET, editor);
}

#endif // COMMANDS_H