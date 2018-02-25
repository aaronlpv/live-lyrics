#ifndef __musixmatch
#define __musixmatch

#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <json-c/json.h>
#include <unistd.h>

#include "lyrics.h"
#include "curl_mem.h"

struct lyric *get_synced_lyrics(const char *artist, const char *track, const char *album, const char *spot_id, int duration);

void init_lyrics();

void lyrics_cleanup();

#endif 
