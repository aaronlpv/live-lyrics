/* Attempts to mimic the official Musixmatch desktop client for Linux.
 * Known differences:
 *  - Does make requests to tracking.musixmatch.com or google analytics. (and as such will not retain cookies acquired from these requests)
 *  - Cookies header does not appear as the last header, as it does in the official client.
 */
#include "musixmatch.h"

const char GUID_FORMAT[] = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
const char HEX_ALPHA[] = "0123456789abcdef";

const char HMAC_KEY[] = "IEJ5E8XFaHQvIQNfs7IC";

const char QUERY[] = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&q_track=%s&q_artist=%s&q_artists=%s&q_album=%s&q_duration=0&user_language=en&f_subtitle_length=0&tags=nowplaying&userblob_id=%s&namespace=lyrics_synched&track_spotify_id=%s&f_subtitle_length_max_deviation=1&subtitle_format=mxm&app_id=web-desktop-app-v1.0&usertoken=%s&guid=%s";
const char TOKEN_QUERY[] = "https://apic-desktop.musixmatch.com/ws/1.1/token.get?format=json&guid=%s&timestamp=%s&build_number=2017091202&lang=en-GB&app_id=web-desktop-app-v1.0";
const char SIGNATURE[] = "&signature=%s&signature_protocol=sha1";

char guid[37];
char usertoken[55];

CURL *curl;
struct curl_slist *headers = NULL;

// Creates a new guid in buf. Apparently this is not how it should be done. But Musixmatch does it this way too, so it's fine.
void new_guid(char *buf){
    int x;
    for(x = 0; x < sizeof(GUID_FORMAT); x++){
        switch(GUID_FORMAT[x]){
            case 'x': buf[x] = HEX_ALPHA[rand() % 16];
                    break;
            case 'y': buf[x] = HEX_ALPHA[ ((rand() % 16) & 3U) | 8U ];
                    break;
            default: buf[x] = GUID_FORMAT[x];
        }
    }
}

// https://journal.missiondata.com/how-to-base64-encode-with-c-c-and-openssl-acccb1045c42
char *base64_encode(const char *in, int in_len){
    BIO *bmem, *b64;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64,BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, in, in_len);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    char *buff = (char *)malloc(bptr->length);
    memcpy(buff, bptr->data, bptr->length-1);
    buff[bptr->length-1] = '\0';

    BIO_free_all(b64);

    return buff;
}

void sign(unsigned char *buf, size_t len){
    unsigned int base64_len;
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

void construct_token_query(char *buf){
    time_t timestamp = time(NULL);
    struct tm *ts = gmtime(&timestamp);
    char time_buf[sizeof("xxxx-xx-xxTxx:xx:xx.xxxZ")];
    strftime(time_buf, sizeof(time_buf), "%FT%T.732Z", ts);

    char *time_esc = curl_easy_escape(curl, time_buf, sizeof(time_buf) - 1);

    int len = sprintf(buf, TOKEN_QUERY, guid, time_esc);

    curl_free(time_esc);

    sign(buf, len);
}

int get_usertoken(char *dest){
    char buf[1000];
    struct string *s = make_string();
    struct json_object *json_root, *json_token;

    construct_token_query(buf);
    curl_easy_setopt(curl, CURLOPT_URL, buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
    curl_easy_perform(curl);

    json_root = json_tokener_parse(s->str);
    if(json_pointer_get(json_root, "/message/body/user_token", &json_token)){
        return 1;
    }

    strcpy(dest, json_object_get_string(json_token));
    json_object_put(json_root);
    free_string(s);
    return 0;
}

void construct_query(char *buf, const char *artist, const char *track, const char *album, const char *spot_id, int duration){
    // TODO: separate artist and artists
    char base64_src[500];
    char *userblob_id;
    char *c = base64_src;

    int leng = sprintf(base64_src, "%s_%s_%d", track, artist, duration);
    while(*c){
        *c = tolower(*c);
        c++;
    }
    userblob_id = base64_encode((unsigned char *)base64_src, strlen(base64_src));

    char *artist_esc = curl_easy_escape(curl, artist, 0);
    char *track_esc = curl_easy_escape(curl, track, 0);
    char *album_esc = curl_easy_escape(curl, album, 0);
    char *spot_esc = curl_easy_escape(curl, spot_id, 0);

    int len = sprintf(buf, QUERY, track_esc, artist_esc, artist_esc, album_esc, userblob_id, spot_esc, usertoken, guid);

    free(userblob_id);

    curl_free(artist_esc);
    curl_free(track_esc);
    curl_free(album_esc);
    curl_free(spot_esc);
    
    sign(buf, len);
}

struct lyric *mxm_get_synced_lyrics(const char *artist, const char *track, const char *album, const char *spot_id, int duration){
    char buf[1000];
    struct json_object *json_root, *json_lyrics_root, *json_iter;
    struct string *s = make_string();
    int i, min, sec, hun;
    char *lyric_str;
    size_t len;
    struct lyric *next = NULL;

    construct_query(buf, artist, track, album, spot_id, duration);

    curl_easy_setopt(curl, CURLOPT_URL, buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
    if(curl_easy_perform(curl) != CURLE_OK){
        printf("bad request!\n");
        free_string(s);
        return 0;
    }

    json_root = json_tokener_parse(s->str);
    free_string(s);
    if(json_pointer_get(json_root, "/message/body/macro_calls/track.lyrics.get/message/body/lyrics/restricted", &json_iter)){
        printf("error!\n");
        return NULL;
    }
    if(json_object_get_int(json_iter) != 0){
        printf("Unauthorised to show lyrics!\n");
        return NULL;
    }
    if(json_pointer_get(json_root, "/message/body/macro_calls/track.subtitles.get/message/body/subtitle_list/0/subtitle/subtitle_body", &json_iter)){
        return NULL;
    }

    json_lyrics_root = json_tokener_parse(json_object_get_string(json_iter));

    json_object_put(json_root);

    len = json_object_array_length(json_lyrics_root);

    for (i = len - 1; i >= 0; i--) {
        json_root = json_object_array_get_idx(json_lyrics_root, i);
        json_object_object_get_ex(json_root, "text", &json_iter);

        lyric_str = (char *) malloc(json_object_get_string_len(json_iter) + 1);
        strcpy(lyric_str, json_object_get_string(json_iter));

        json_object_object_get_ex(json_root, "time", &json_root);
        json_object_object_get_ex(json_root, "minutes", &json_iter);
        min = json_object_get_int(json_iter);
        json_object_object_get_ex(json_root, "seconds", &json_iter);
        sec = json_object_get_int(json_iter);
        json_object_object_get_ex(json_root, "hundredths", &json_iter);
        hun = json_object_get_int(json_iter);

        next = make_lyric(lyric_str, min, sec, hun, next);
    }

    json_object_put(json_lyrics_root);

    return next;
}

int write_persistent_file(char *path){
    FILE *file;
    // generate a new guid and request a new usertoken
    new_guid(guid);
    if(get_usertoken(usertoken)){
        // if the server doesn't want to respond, there is nothing we can do
        fprintf(stderr, "Could not get usertoken!\n");
        exit(1);
    }

    if((file = fopen(path, "w")) == NULL){
        return 1;
    }
    
    fputs(guid, file);
    fputs(usertoken, file);
    fclose(file);
    return 0;
}

void mxm_init(){
    char path[1000];
    FILE *file;
    int len;

    srand(time(NULL));

    curl = curl_easy_init();
    // replicate Electron headers
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Musixmatch/0.19.4 Chrome/58.0.3029.110 Electron/1.7.6 Safari/537.36");
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate");
    headers = curl_slist_append(headers, "Accept-Language: en-US");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // make sure the AWS Elastic Load Balancing cookie is set, or the server will refuse to talk with us
    curl_easy_setopt(curl, CURLOPT_COOKIE, "AWSELB=unknown");

    char *dir = getenv("XDG_CONFIG_HOME");
    if(dir){
        len = sprintf(path, "%s/nucleonlyrics/", dir);
    }else{
        dir = getenv("HOME");
        if(dir){
            len = sprintf(path, "%s/.config/nucleonlyrics/", dir);
        }else{
            fprintf(stderr, "Can't find your config folder, check your environment variables.\n");
            exit(EXIT_FAILURE);
        }
    }

    // Create the nucleonlyrics folder if it does not exist.
    if (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1){
        if( errno != EEXIST ) {
            fprintf(stderr, "Could not create config folder!\n");
            exit(EXIT_FAILURE);
        }
    }

    // this next part is quite the mess and needs to be redone
    sprintf(&path[len], "%s", "persistent.txt");
    if((file = fopen(path, "r")) != NULL){
        if(fgets(guid, sizeof(guid), file) == NULL || fgets(usertoken, sizeof(usertoken), file) == NULL){
            // persistent.txt is corrupted
            fclose(file);
            if(write_persistent_file(path)){
                fprintf(stderr, "Could not overwrite persistent file!\n");
            }
        }else{
            // persistent.txt is valid and we can read everything we need
            fclose(file);
            sprintf(&path[len], "%s", "cookies.txt");
            if(access(path, F_OK) != -1) {
                // persistent.txt is valid and cookies.txt exists => read cookies
                curl_easy_setopt(curl, CURLOPT_COOKIEFILE, path);
            }
        }
    }else{
        // persistent.txt does not exist
        if(write_persistent_file(path)){
            fprintf(stderr, "Could not create persistent file!\n");
            exit(1);
        }
    }
    sprintf(&path[len], "%s", "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, path);
}

void mxm_cleanup(){
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}