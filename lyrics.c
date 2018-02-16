#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <json-c/json.h>

const char GUID_FORMAT[] = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
const char HEX_ALPHA[] = "0123456789abcdef";

const char HMAC_KEY[] = "IEJ5E8XFaHQvIQNfs7IC";

const char QUERY[] = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&q_track=%s&q_artist=%s&q_artists=%s&q_album=%s&q_duration=0&user_language=en&f_subtitle_length=0&tags=nowplaying&userblob_id=%s&namespace=lyrics_synched&track_spotify_id=%s&f_subtitle_length_max_deviation=1&subtitle_format=mxm&app_id=web-desktop-app-v1.0&usertoken=%s&guid=%s";
const char TOKEN_QUERY[] = "https://apic-desktop.musixmatch.com/ws/1.1/token.get?format=json&guid=%s&timestamp=%s&build_number=2017091202&lang=en-GB&app_id=web-desktop-app-v1.0";
const char SIGNATURE[] = "&signature=%s&signature_protocol=sha1";

char guid[37];
char usertoken[55];

CURL *curl;

// Creates a new guid in buf. Apparently this is not how it should be done. But Musixmatch does it this way too, so it's fine.
void new_guid(char *buf){
    int x;
    for(x = 0; x < sizeof GUID_FORMAT; x++){
        switch(GUID_FORMAT[x]){
            case 'x': buf[x] = HEX_ALPHA[rand() % 16];
                    break;
            case 'y': buf[x] = HEX_ALPHA[(rand() % 16) & 3U | 8U];
                    break;
            default: buf[x] = GUID_FORMAT[x];
        }
    }
}

typedef struct string {
  char *str;
  size_t len;
} string;

string *make_string(){
  string *res = (string *) malloc(sizeof(struct string));
  res->len = 0;
  res->str = (char *) malloc(1);
  *(res->str) = '\0';
  return res;
}

/* https://curl.haxx.se/libcurl/c/getinmemory.html */
size_t writefunc(void *contents, size_t size, size_t nmemb, void *userp){
  string *s = userp;
  size_t realsize = size * nmemb;
 
  s->str = realloc(s->str, s->len + realsize + 1);
 
  memcpy(&(s->str[s->len]), contents, realsize);
  s->len += realsize;
  s->str[s->len] = '\0';
 
  return realsize;
}

void free_string(string *s){
  free(s->str);
  free(s);
}

// https://journal.missiondata.com/how-to-base64-encode-with-c-c-and-openssl-acccb1045c42
char *base64_encode(const unsigned char *in, int in_len){
  BIO *bmem, *b64;
  BUF_MEM *bptr;

  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_write(b64, in, in_len);
  BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buff = (char *)malloc(bptr->length);
  memcpy(buff, bptr->data, bptr->length-1);
  buff[bptr->length-1] = 0;

  BIO_free_all(b64);

  return buff;
}

void sign(char *buf, size_t len){
    int base64_len;
    char *base64_sig = NULL;
    char base64_buf[EVP_MAX_MD_SIZE];

    time_t timestamp = time(NULL);
    struct tm *ts = gmtime(&timestamp);
    size_t date_size = strftime(&buf[len], 9, "%Y%m%d", ts);

    HMAC(EVP_sha1(), (unsigned char*)HMAC_KEY, strlen(HMAC_KEY), (unsigned char*)buf, len + date_size, (unsigned char*)&base64_buf, &base64_len);

    base64_sig = base64_encode(base64_buf, base64_len);

    char *hmac_esc = curl_easy_escape(curl, base64_sig, 0);

    sprintf(&buf[len], SIGNATURE, hmac_esc);
    curl_free(hmac_esc);
    free(base64_sig);
}

void construct_query(char *buf, const char *artist, const char *track, const char *album, const char *spot_id, int duration){
    char base64_src[500];
    char *userblob_id;
    char *c = base64_src;

    sprintf(base64_src, "%s_%s_%d", track, artist, duration);
    while(*c){
        *c = tolower(*c);
        c++;
    }
    userblob_id = base64_encode(base64_src, strlen(base64_src));

    char *artist_esc = curl_easy_escape(curl, artist, 0);
    char *track_esc = curl_easy_escape(curl, track, 0);
    char *album_esc = curl_easy_escape(curl, album, 0);
    char *spot_esc = curl_easy_escape(curl, spot_id, 0);

    sprintf(buf, QUERY, track_esc, artist_esc, artist_esc, album_esc, userblob_id, spot_esc, usertoken, guid);

    free(userblob_id);

    curl_free(artist_esc);
    curl_free(track_esc);
    curl_free(album_esc);
    curl_free(spot_esc);
    
    sign(buf, strlen(buf));
}
void construct_token_query(char *buf){
    time_t timestamp = time(NULL);
    struct tm *ts = gmtime(&timestamp);
    char time_buf[sizeof "xxxx-xx-xxTxx:xx:xx.xxxZ"];
    strftime(time_buf, sizeof time_buf, "%FT%T.732Z", ts);

    char *time_esc = curl_easy_escape(curl, time_buf, sizeof time_buf - 1);

    int len = sprintf(buf, TOKEN_QUERY, guid, time_esc);

    curl_free(time_esc);

    sign(buf, len);
}

// this needs to be cleaned up
int get_usertoken(){
    char buf[1000];
    string *s = make_string();
    struct json_object *json_root;
    struct json_object *json_msg;
    struct json_object *json_body;
    struct json_object *json_token;

    construct_token_query(buf);
    curl_easy_setopt(curl, CURLOPT_URL, buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
    curl_easy_perform(curl);

    json_root = json_tokener_parse(s->str);
    if(!json_root){
        return 1;
    }
    json_object_object_get_ex(json_root, "message", &json_msg);
    if(!json_msg){
        return 1;
    }
    json_object_object_get_ex(json_msg, "body", &json_body);
    if(!json_body){
        return 1;
    }
    json_object_object_get_ex(json_body, "user_token", &json_token);
    if(!json_token){
        return 1;
    }

    strcpy(usertoken, json_object_get_string(json_token));
    json_object_put(json_root);
    free_string(s);
    return 0;
}

void init_lyrics(){
    char path[1000];

    curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Musixmatch/0.19.4 Chrome/58.0.3029.110 Electron/1.7.6 Safari/537.36");
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate");
    headers = curl_slist_append(headers, "Accept-Language: en-US");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    char *dir = getenv("XDG_CONFIG_HOME");
    if(dir){
        sprintf(path, "%s/nucleonlyrics/cookies.txt", dir);
    }else{
        dir = getenv("HOME");
        if(dir){
            sprintf(path, "%s/.config/nucleonlyrics/cookies.txt", dir);
        }else{
            fprintf(stderr, "Can't find cookie file, check your environment variables.");
            exit(EXIT_FAILURE);
        }
    }
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, path);
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, path);

    srand(time(NULL));

    new_guid(guid);

    if(get_usertoken()){
        fprintf(stderr, "Could not get usertoken!");
        exit(1);
    }
}


void lyrics_cleanup(){
    curl_easy_cleanup(curl);
}

int main(){
    init_lyrics();
    char buf[1000];

    construct_query(buf, "Abel", "Onderweg", "De Stilte Voorbij", "spotify:track:1EtcyegB7JLkAwwqiPyeJ6", 189);
    printf("%s\n", buf);
    curl_easy_setopt(curl, CURLOPT_URL, buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_perform(curl);
    
    lyrics_cleanup();
}
