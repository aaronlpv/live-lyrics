#include <curl/curl.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <json-c/json.h>
#include <string.h>
#include <unistd.h>

#include "curl_mem.h"
#include "spotify.h"

#define OAUTH_LEN 68+1
#define CSRF_LEN 32+1

const char local_token[] = "127.0.0.1:%d/simplecsrf/token.json";
const char local_remote[] = "127.0.0.1:%d/remote/%%s.json?oauth=%s&csrf=%s%%s";
const char url_spotify[] = "https://open.spotify.com/token";

static CURL *curl = NULL;
static struct curl_slist *curl_headers;

int port = 4381;
char oauth[OAUTH_LEN];
char csrf[CSRF_LEN];
char base_url[1000];

void spotify_free_resource(struct spotify_resource *res){
  free(res->name);
  free(res->uri);
  free(res);
}

void spotify_free_track(struct spotify_track *tr){
  spotify_free_resource(tr->track);
  spotify_free_resource(tr->artist);
  spotify_free_resource(tr->album);
  free(tr);
}

void spotify_free_playback(struct spotify_playback *pb){
  spotify_free_track(pb->track);
  free(pb);
}

void spotify_command(char *cmd, char *args, struct string *s){
  char url[1000];

  sprintf(url, base_url, cmd, args);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  if(s == NULL){
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
  }else{
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
  }
  curl_easy_perform(curl);
}

int get_oauth_token(){
  struct string *s = make_string();
  struct json_object *json_root;
  struct json_object *json_token;

  curl_easy_setopt(curl, CURLOPT_URL, url_spotify);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);

  curl_easy_perform(curl);

  json_root = json_tokener_parse(s->str);
  if(!json_root){
    return 1;
  }
  json_object_object_get_ex(json_root, "t", &json_token);
  strncpy(oauth, json_object_get_string(json_token), sizeof(oauth));

  json_object_put(json_root);
  free_string(s);
  return 0;
}


int spotify_init(){
  struct string *s = make_string();
  struct json_object *json_root, *json_o;
  char url[1000];
  if(curl){
    // already initialized
    return 0;
  }
  curl = curl_easy_init();
  curl_headers = curl_slist_append(curl_headers, "Origin: https://open.spotify.com");
  curl_headers = curl_slist_append(curl_headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:59.0) Gecko/20100101 Firefox/59.0");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem);

  if(get_oauth_token()){
    fprintf(stderr, "Could not get OAUTH token!");
    exit(EXIT_FAILURE);
  }

  free_string(s);
  s = make_string();
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);

  sprintf(url, local_token, port);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_perform(curl);
  json_root = json_tokener_parse(s->str);
  json_object_object_get_ex(json_root, "token", &json_o);
  strcpy(csrf, json_object_get_string(json_o));
  json_object_put(json_root);

  free_string(s);

  sprintf(base_url, local_remote, port, oauth, csrf);

  return 0;
}

void spotify_cleanup(){
  curl_easy_cleanup(curl);
  curl_slist_free_all(curl_headers);
}

struct spotify_resource *spotify_json_resource(json_object *root, char *base){
  json_object *temp;
  char *str;
  struct spotify_resource *res = malloc(sizeof(struct spotify_resource));

  json_object_object_get_ex(root, base, &root);
  json_object_object_get_ex(root, "name", &temp);
  str = malloc(json_object_get_string_len(temp) + 1);
  strcpy(str, json_object_get_string(temp));
  res->name = str;
  json_object_object_get_ex(root, "uri", &temp);
  str = malloc(json_object_get_string_len(temp) + 1);
  strcpy(str, json_object_get_string(temp));
  res->uri = str;
  return res;
}

struct spotify_playback *spotify_status(){
  struct string *s = make_string();
  struct json_object *json_root, *json_res, *json_temp;
  struct spotify_playback *sp;
  const char *type;

  sp = malloc(sizeof(struct spotify_playback));
  sp->track = malloc(sizeof(struct spotify_track));
  spotify_command("status", "", s);

  json_root = json_tokener_parse(s->str);
  json_object_object_get_ex(json_root, "track", &json_res);
  json_object_object_get_ex(json_res, "track_type", &json_temp);
  type = json_object_get_string(json_temp);
  if(strcmp(type, "normal") == 0 || strcmp(type, "explicit") == 0){ // explicit is absolutely meaningless
    sp->track->type = normal;
    sp->track->track = spotify_json_resource(json_res, "track_resource");
    sp->track->artist = spotify_json_resource(json_res, "artist_resource");
    sp->track->album = spotify_json_resource(json_res, "album_resource");
  }else if(strcmp(type, "ad") == 0){
    sp->track->type = ad;
    sp->track->track = NULL;
    sp->track->artist = NULL;
    sp->track->album = NULL;
  }else{
    fprintf(stderr, "[Spotify] Unknown track type: %s\n", type);
    exit(1);
  }

  json_object_object_get_ex(json_res, "length", &json_res);
  sp->track->length = json_object_get_int(json_res);

  json_object_object_get_ex(json_root, "playing", &json_res);
  sp->playing = json_object_get_boolean(json_res);

  json_object_object_get_ex(json_root, "playing_position", &json_res);
  sp->position = json_object_get_double(json_res);

  json_object_put(json_root);
  free_string(s);
  return sp;
}

/*int main(){
  struct spotify_playback *sp;
  struct lyric *lrc;

  spotify_init();
  init_lyrics();
  //spotify_command("status", "", s); // "&returnon=play,pause,login,logout,error,ap&returnafter=0", s);

  sp = spotify_status();
  if(sp->track->type == ad){
    printf("Ad is playing...\n");
    exit(0);
  }
  printf("Track: %s\n", sp->track->track->name);
  printf("Artist: %s\n", sp->track->artist->name);
  printf("Album: %s\n", sp->track->album->name);
  printf("%f\n", sp->position);

  lrc = get_synced_lyrics(sp->track->artist->name, sp->track->track->name, sp->track->album->name, sp->track->track->uri, sp->track->length);

  time_t past = time(NULL);
  time_t now;
  double position = sp->position;
  struct lyric *curr = lrc;
  int next = curr->min * 60 + curr->sec;
  while(curr != NULL){
    now = time(NULL);
    position += now - past;
    past = now;
    if(position > next){
      printf("[%02d:%02d:%02d] %s\n", curr->min, curr->sec, curr->hun, curr->str);
      curr = curr->next;
      next = curr->min * 60 + curr->sec + (curr->hun >= 50 ? 1 : 0);
    }
    usleep(100);
  }

  free_lyric(lrc);
  spotify_free_playback(sp);
  lyrics_cleanup();
  spotify_cleanup();
  return 0;
}*/