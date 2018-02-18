#include "lyrics.h"

struct lyric *make_lyric(char *str, char min, char sec, char hun, struct lyric *next){
    struct lyric *res = malloc(sizeof(struct lyric));
    res->str = str;
    res->min = min;
    res->sec = sec;
    res->hun = hun;
    res->next = next;
    return res;
}

void free_lyric(struct lyric *l){
    struct lyric *next;
    while(l != NULL){
        next = l->next;
        free(l->str);
        free(l);
        l = next;
    }
}