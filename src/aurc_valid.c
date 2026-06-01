#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

char *validActions[] = {
    "install", "install-force", "remove", "query", "search", "remove-dep",
    "clear-aur-cache", "update", "self-update", "refresh", "modify-repo", "remove-force",
    "remove-force-dep", "search-aur", "install-aur", "install-local", "remove-orp",
    "list-aur", "github", "config"};

size_t numValidActions = sizeof(validActions) / sizeof(validActions[0]);

void __attribute__((constructor)) createHashSet()
{
    hcreate(numValidActions);
    ENTRY e;

    for (size_t i = 0; i < numValidActions; i++)
    {
        e.key = validActions[i];
        e.data = (void *)0;
        hsearch(e, ENTER);
    }
}

int isValidAction(const char *action)
{
    if (action == NULL)
    {
        return 0;
    }

    ENTRY e, *ep;
    e.key = (char *)action;
    ep = hsearch(e, FIND);

    return ep != NULL;
}