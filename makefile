all: lyrics.c
	gcc `pkg-config --cflags --libs xcb cairo-xcb xcb-ewmh xcb-shape json-c openssl libcurl` -o lyrics main.c spotify.c musixmatch.c curl_mem.c lyrics.c
