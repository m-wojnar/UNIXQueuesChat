CC = gcc
FLAGS = -Wall


all: server client


server: server.c
	$(CC) $(FLAGS) server.c -o server -lrt


client: client.c
	$(CC) $(FLAGS) client.c -o client -lrt
	
	
test: all
	# serwer
	./server

	#klient
	./client


clean:
	rm -f server
	rm -f client
