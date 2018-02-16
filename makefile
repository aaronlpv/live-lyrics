all: lyrics.c
	gcc -lcrypto -lcurl -ljson-c -o lyrics lyrics.c
