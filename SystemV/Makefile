CC = gcc
FLAGS = -Wall


all: server client


server: server.c
	$(CC) $(FLAGS) server.c -o server


client: client.c
	$(CC) $(FLAGS) client.c -o client
	
	
test: all
	# serwer
	./server

	#klient
	./client


clean:
	rm -f server
	rm -f client
