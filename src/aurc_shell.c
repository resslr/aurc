#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include "colors.h"

static char *getCurrentUserShell()
{
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL || pw->pw_shell == NULL)
    {
        perror("getpwuid");
        return NULL;
    }

    char *shellName = strrchr(pw->pw_shell, '/');
    shellName = shellName ? shellName + 1 : pw->pw_shell;

    char *shell = strdup(shellName);
    if (!shell)
        perror("strdup");
    return shell;
}

int executeCommandWithUserShell(const char *command)
{
    if (!command || command[0] == '\0')
    {
        fprintf(stderr, "Invalid command.\n");
        return -1;
    }

    char *userShell = getCurrentUserShell();
    if (!userShell)
    {
        fprintf(stderr, "Unable to detect shell, falling back to system().\n");
        int r = system(command);
        printf(RESET);
        return r;
    }

    char *argv[] = {userShell, "-c", (char *)command, NULL};

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        free(userShell);
        return -1;
    }
    else if (pid == 0)
    {
        execvp(userShell, argv);
        perror("execvp");
        _exit(EXIT_FAILURE);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid");
        free(userShell);
        return -1;
    }

    free(userShell);
    printf(RESET);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}
