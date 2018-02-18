all: lyrics.c
	gcc -lcrypto -lcurl -ljson-c -o lyrics musixmatch.c curl_mem.c lyrics.c
