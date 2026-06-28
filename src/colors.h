#ifndef AURC_COLORS_H
#define AURC_COLORS_H

#include <stdio.h>
#include <string.h>

// ANSI escape codes for colors
#define RED "\033[1;31m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define BLUE "\033[1;34m"
#define MAGENTA "\033[1;35m"
#define CYAN "\033[1;36m"
#define BOLD "\033[1m"
#define GRAY "\033[0;90m"
#define RESET "\033[0m"

static inline void printSection(const char *msg)
{
    printf("\n" CYAN BOLD "::" RESET BOLD " %s" RESET "\n\n", msg);
}

static inline void printBox(const char *title)
{
    size_t len = strlen(title);
    printf("\n" CYAN "╭");
    for (size_t i = 0; i < len + 2; i++) printf("─");
    printf("╮\n│ " RESET BOLD "%s" RESET CYAN " │\n╰", title);
    for (size_t i = 0; i < len + 2; i++) printf("─");
    printf("╯" RESET "\n");
}

#endif // AURC_COLORS_H
