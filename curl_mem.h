#ifndef __curl_mem
#define __curl_mem

#include <malloc.h>
#include <string.h>

struct string {
  char *str;
  size_t len;
};

size_t curl_write_mem(void *contents, size_t size, size_t nmemb, void *userp);

struct string *make_string();

void free_string(struct string *s);


#endif