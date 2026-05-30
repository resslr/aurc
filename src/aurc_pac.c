#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "constants.h"
#include "colors.h"

int existingAurPackage(const char *packageName);
void installAurPackages(char **packageNames, unsigned int numPackages);

static int packageInRepos(const char *name)
{
    pid_t pid = fork();
    if (pid == -1)
        return 0;
    if (pid == 0)
    {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        execlp("pacman", "pacman", "-Si", "--", name, NULL);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void executePacmanCommand(int argc, char *argv[], const char *commandPrefix, const char *usageMessage)
{
    if (argc < 3)
    {
        fprintf(stderr, RED "Usage: %s %s\n" RESET, argv[0], usageMessage);
        return;
    }

    size_t commandLength = strlen(commandPrefix);
    for (int i = 2; i < argc; ++i)
        commandLength += strlen(argv[i]) + 1;

    if (commandLength > MAX_COMMAND_LENGTH)
    {
        fprintf(stderr, "Command too long\n");
        return;
    }

    char *command = malloc(commandLength + 1);
    if (!command)
    {
        perror(RED "malloc" RESET);
        return;
    }

    strncpy(command, commandPrefix, commandLength);
    for (int i = 2; i < argc; ++i)
    {
        strncat(command, argv[i], commandLength - strlen(command) - 1);
        if (i < argc - 1)
            strncat(command, " ", commandLength - strlen(command) - 1);
    }
    executeCommandWithUserShell(command);
    free(command);
}

void installLocalPackages(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, RED "Usage: %s install-local <packagePath>\n" RESET, argv[0]);
        return;
    }

    char command[MAX_COMMAND_LENGTH];
    strncpy(command, "sudo pacman -U ", MAX_COMMAND_LENGTH - 1);
    command[MAX_COMMAND_LENGTH - 1] = '\0';
    strncat(command, argv[2], MAX_COMMAND_LENGTH - strlen(command) - 1);
    system(command);
}

void removePackagesForce(int argc, char *argv[])
{
    executePacmanCommand(argc, argv, REMOVE_FORCE_COMMAND, "remove-force <package1> ...");
}

void installPackagesForce(int argc, char *argv[])
{
    executePacmanCommand(argc, argv, INSTALL_FORCE_COMMAND, "install-force <package1> ...");
}

void removePackagesForceWithDependencies(int argc, char *argv[])
{
    executePacmanCommand(argc, argv, "sudo pacman -Rdds ", "remove-force-dep <package1> ...");
}

void installPackages(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, RED "Usage: %s install <package1> <package2> ...\n" RESET, argv[0]);
        return;
    }

    int numPackages = argc - 2;
    char **packages = argv + 2;

    char *repoPackages[numPackages];
    char *aurPackages[numPackages];
    int repoCount = 0, aurCount = 0;

    for (int i = 0; i < numPackages; i++)
    {
        if (packageInRepos(packages[i]))
        {
            repoPackages[repoCount++] = packages[i];
        }
        else
        {
            printf("'%s' not in official repos, checking AUR...\n", packages[i]);
            if (existingAurPackage(packages[i]))
            {
                printf(YELLOW "Found '%s' in AUR.\n" RESET, packages[i]);
                aurPackages[aurCount++] = packages[i];
            }
            else
            {
                fprintf(stderr, RED "Package '%s' not found in any repository.\n" RESET, packages[i]);
            }
        }
    }

    if (repoCount > 0)
    {
        char command[MAX_COMMAND_LENGTH];
        strncpy(command, "sudo pacman -S", MAX_COMMAND_LENGTH - 1);
        command[MAX_COMMAND_LENGTH - 1] = '\0';
        for (int i = 0; i < repoCount; i++)
        {
            strncat(command, " ", MAX_COMMAND_LENGTH - strlen(command) - 1);
            strncat(command, repoPackages[i], MAX_COMMAND_LENGTH - strlen(command) - 1);
        }
        executeCommandWithUserShell(command);
    }

    if (aurCount > 0)
    {
        char answer[16];
        printf("Install %d AUR package%s (%s) via makepkg? (y/n): ",
               aurCount, aurCount == 1 ? "" : "s",
               aurPackages[0]);
        fgets(answer, sizeof(answer), stdin);
        if (!strchr(answer, '\n')) { int c; while ((c = getchar()) != '\n' && c != EOF); }
        if (tolower(answer[0]) == 'y' || answer[0] == '\n')
            installAurPackages(aurPackages, (unsigned int)aurCount);
    }
}

void removePackages(int argc, char *argv[])
{
    executePacmanCommand(argc, argv, "sudo pacman -R ", "remove <package1> ...");
}

void queryPackage(char *packageName)
{
    char command[MAX_COMMAND_LENGTH];
    snprintf(command, sizeof(command), "pacman -Q %s", packageName);
    int result = system(command);
    if (result != 0)
        fprintf(stderr, RED "%s is not installed.\n" RESET, packageName);
    else
        printf(GREEN "%s is installed.\n" RESET, packageName);
}

void searchPackage(char *packageName)
{
    char command[MAX_COMMAND_LENGTH];
    snprintf(command, sizeof(command), "pacman -Ss %s", packageName);
    system(command);
}

void removePackagesWithDependencies(int argc, char *argv[])
{
    executePacmanCommand(argc, argv, "sudo pacman -Rs ", "remove-dep <package1> ...");
}

void removeOrphanPackages()
{
    executeCommandWithUserShell("sudo pacman -Rns $(pacman -Qdtq)");
}

void updateSystem()
{
    executeCommandWithUserShell("sudo pacman -Syyu");
}

void refreshRepo()
{
    executeCommandWithUserShell("sudo pacman -Syy");
}

void modifyRepo()
{
    char editor[256] = "vi";

    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, "Could not get home directory\n");
        exit(1);
    }

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
    int ret = snprintf(command, commandSize, "sudo %s /etc/pacman.d/mirrorlist", editor);
    if (ret < 0 || (size_t)ret >= commandSize)
    {
        fprintf(stderr, "Failed to build command\n");
        return;
    }

    executeCommandWithUserShell(command);
}
