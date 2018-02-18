#include "curl_mem.h"

/* https://curl.haxx.se/libcurl/c/getinmemory.html */
size_t curl_write_mem(void *contents, size_t size, size_t nmemb, void *userp){
  struct string *s = userp;
  size_t realsize = size * nmemb;
 
  s->str = realloc(s->str, s->len + realsize + 1);
 
  memcpy(&(s->str[s->len]), contents, realsize);
  s->len += realsize;
  s->str[s->len] = '\0';
 
  return realsize;
}

struct string *make_string(){
  struct string *res = malloc(sizeof(struct string));
  res->len = 0;
  res->str = (char *) malloc(1);
  *(res->str) = '\0';
  return res;
}

void free_string(struct string *s){
  free(s->str);
  free(s);
}