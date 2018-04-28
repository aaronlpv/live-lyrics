#ifndef __spotify
#define __spotify

#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <malloc.h>

struct spotify_resource{
  char *name;
  char *uri;
};

struct spotify_track{
  struct spotify_resource *track;
  struct spotify_resource *artist;
  struct spotify_resource *album;
  enum {Normal, Ad} type;
  int length;
};

struct spotify_playback{
  struct spotify_track *track;
  double position;
  int playing;
};

void spotify_free_resource(struct spotify_resource *res);

void spotify_free_track(struct spotify_track *tr);

void spotify_free_playback(struct spotify_playback *pb);

void spotify_free_track(struct spotify_track *tr);

int spotify_init();

void spotify_cleanup();

struct spotify_playback *spotify_status();

#endif