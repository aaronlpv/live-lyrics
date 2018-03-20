all: lyrics.c
	gcc `pkg-config --cflags --libs xcb cairo-xcb xcb-ewmh json-c openssl libcurl` -o lyrics xcb_test.c spotify.c musixmatch.c curl_mem.c lyrics.c
