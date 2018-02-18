#ifndef __lyrics
#define __lyrics

#include <malloc.h>

struct lyric {
    char *str;
    char min;
    char sec;
    char hun;
    struct lyric *next;
};

struct lyric *make_lyric(char *str, char min, char sec, char hun, struct lyric *next);

void free_lyric(struct lyric *l);

#endif