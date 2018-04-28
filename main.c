#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/shape.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <cairo-xcb.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#include "spotify.h"
#include "musixmatch.h"
#include "lyrics.h"

#define BLACK 8.0/255.0
#define RADIUS 2.0
#define DEG90 90 * (M_PI/180.0)

#define MOUSE_ALPHA 0.25
#define DEF_ALPHA 1.0

int win_width = 1920;
int win_height = 60;

int keep_running = 1;

xcb_pixmap_t shape;

// gcc `pkg-config --cflags --libs xcb cairo-xcb xcb-ewmh json-c openssl libcurl` -o test xcb_test.c spotify.c musixmatch.c curl_mem.c lyrics.c

void int_handler(int sig){
    keep_running = 0;
}

int main(){
    /* properly clean everything up on CTRL-C */
    signal(SIGINT, int_handler);

    /* Connect to X server */
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    xcb_depth_t *depth = NULL;
    while (depth_iter.rem) {
        if (depth_iter.data->depth == 32 && depth_iter.data->visuals_len) {
            depth = depth_iter.data;
            break;
        }
        xcb_depth_next(&depth_iter);
    }

    if (!depth) {
        fprintf(stderr, "Screen does not support 32 bit color depth!\n");
        xcb_disconnect(connection);
        return -1;
    }
    
    xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth);
    xcb_visualtype_t *visual = NULL;
    while (visual_iter.rem) {
        if (visual_iter.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
            visual = visual_iter.data;
            break;
        }
        xcb_visualtype_next(&visual_iter);
    }

    if (!visual) {
        fprintf(stderr, "ERROR: screen does not support True Color\n");
        xcb_disconnect(connection);
        return -1;
    }

    /* Create 32 bit color map */
    xcb_colormap_t colormap = xcb_generate_id(connection);
    xcb_create_colormap( connection,
                        XCB_COLORMAP_ALLOC_NONE,
                        colormap,
                        screen->root,
                        visual->visual_id);
    
    unsigned int cw_values[] = { 0, 0, 1, XCB_EVENT_MASK_EXPOSURE, colormap };
    xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window( connection,                             /* Connection       */
                        depth->depth,                           /* depth (32 bits)  */
                        window,                                 /* window id        */
                        screen->root,                           /* parent window    */
                        10, 1000,                                /* x, y             */
                        win_width, win_height,                  /* width, height    */
                        1,                                      /* border_width     */
                        XCB_WINDOW_CLASS_INPUT_OUTPUT,          /* class            */
                        visual->visual_id,                      /* visual           */
                        XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                        cw_values);

    // this is very lame, but it's seemingly the only way to create a semi-transparent clickthrough overlay
    shape = xcb_generate_id(connection);
    xcb_create_pixmap (connection,
                       1,     /* depth of the pixmap */
                       shape,  /* id of the pixmap */
                       window, /* conventiently not explained in xcb tutorial, just great */
                       win_width,     /* pixel width of the window */
                       win_height);  /* pixel height of the window */
    xcb_shape_mask(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, window, 0, 0, shape);

    xcb_map_window(connection, window);
    cairo_surface_t *surface = cairo_xcb_surface_create (connection,
                        window,
                        visual,
                        win_width, win_height);
    cairo_t *cr = cairo_create(surface);
    xcb_flush(connection);

    struct spotify_playback *sp;
    struct lyric *lrc;

    if(!spotify_init()){
        fprintf(stderr, "Could not initialize Spotify!\n");
        exit(EXIT_FAILURE);
    }
    mxm_init();

    sp = spotify_status();
    if(sp->track->type == Ad){
        printf("Ad is playing...\n");
        exit(0);
    }
    printf("\e[36m%s\e[0m by \e[33m%s\e[0m\n", sp->track->track->name, sp->track->artist->name);

    lrc = mxm_get_synced_lyrics(sp->track->artist->name, sp->track->track->name, sp->track->album->name, sp->track->track->uri, sp->track->length);
    if(lrc == NULL){
        fprintf(stderr, "lyric error\n");
        exit(-1);
    }
    spotify_free_playback(sp);

    struct timespec tp, now, last_draw;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    double position = sp->position;
    struct lyric *curr = lrc;
    long next = curr->next->min * 60 * 100 + curr->next->sec * 100 + curr->next->hun;
    int should_redraw = 1;

    xcb_generic_event_t *event;
    while (keep_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long position_now = position * 100 + (now.tv_sec - tp.tv_sec) * 100 + ((double) now.tv_nsec - tp.tv_nsec) / 1E7;
        while(position_now > next){
            curr = curr->next;
            printf("[%02d:%02d.%02d] %s\n", curr->min, curr->sec, curr->hun, curr->str);
            next = curr->next->min * 60 * 100 + curr->next->sec * 100 + curr->next->hun;
            // TODO: fix segfault at end of song and get next song
            should_redraw = 1;
        }

        while( (event = xcb_poll_for_event (connection)) != NULL ){ 
            switch (event->response_type & ~0x80) {
                case XCB_EXPOSE:
                should_redraw = 1;
                break;
            }
        }

        if(should_redraw){
            clock_gettime(CLOCK_MONOTONIC, &last_draw);
            xcb_clear_area(connection, 0, window, 0, 0, win_width, win_height);

            cairo_select_font_face (cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size (cr, 32.0);
            cairo_font_extents_t font_extents;
            cairo_text_extents_t text_extents;
            cairo_font_extents(cr, &font_extents);

            double padding = 8.0;
            double x       = 0,
                   y       = 0,
                   width,
                   height  = font_extents.ascent + padding;
            double text_x  = x + padding;
            double text_y  = y + font_extents.ascent;

            cairo_text_extents(cr, curr->str, &text_extents);
            width = text_extents.x_advance + padding * 2;
            
            cairo_new_path (cr);
            cairo_arc (cr, x - RADIUS + width, y + RADIUS,          RADIUS,    -DEG90,           0); /* TR */
            cairo_arc (cr, x - RADIUS + width, y - RADIUS + height, RADIUS,         0,       DEG90); /* BR */
            cairo_arc (cr, x + RADIUS,         y - RADIUS + height, RADIUS,     DEG90,   2 * DEG90); /* BL */
            cairo_arc (cr, x + RADIUS,         y + RADIUS,          RADIUS, 2 * DEG90,   3 * DEG90); /* TL */
            cairo_close_path (cr);
            cairo_set_source_rgba (cr, BLACK, BLACK, BLACK, 0.75);
            cairo_fill(cr);

            cairo_move_to (cr, text_x, text_y);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            cairo_show_text (cr, curr->str);

            cairo_surface_flush(surface);
            xcb_flush(connection);
            should_redraw = 0;
        }
        usleep(130000);
    }

    printf("Goodbye\n");
    free_lyric(lrc);
    mxm_cleanup();
    spotify_cleanup();
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    xcb_free_pixmap(connection, shape);
    xcb_disconnect(connection);
    return 0;
}
