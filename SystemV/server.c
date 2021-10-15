#include <stdlib.h>
#include <stdio.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include "config.h"

// obsluga petli zdarzen
void messages_handler();

// funkcje do obslugi polecen otrzymanych od klientow
void init(key_t client_key);
void list(int client_id);
int is_available(int id);
void connect(int client_id, int connect_to_id);
void disconnect(int client_id);
void stop(int client_id);

// obsluga zamkniecia serwera i sygnalu SIGINT
void exit_handler();
int all_disconnected();
void sigint_handler(int signal);

// enum i struktura do opisu klienta
enum client_state {
    CONNECTED = 1,
    CHATTING = 2,
    DISCONNECTED = 3
};

struct client_info {
    enum client_state state;
    int queue_id;
    key_t queue_key;
};

struct client_info clients[MAX_CLIENTS];
int next_id = 1;
int queue_id;

int main() {
    // ustawienie obslugi wyjscia z serwera i sygnalu SIGINT
    atexit(exit_handler);
    signal(SIGINT, sigint_handler);

    // utworzenie kolejki serwera
    key_t key = ftok(getenv("HOME"), PROJ_ID);
    queue_id = msgget(key, IPC_CREAT | 0666);

    // petla zdarzen
    while (1) 
        messages_handler();

    return 0;
}

// oczekiwanie na polecenie i wybor funkcji do obslugi polecenia
void messages_handler() {
    struct message buffer;
    msgrcv(queue_id, &buffer, MESSAGE_SIZE, -6, 0);

    switch (buffer.mtype) {
        case INIT:
            init(buffer.value);
            break;
        case LIST:
            list(buffer.sender_id);
            break;
        case CONNECT:
            connect(buffer.sender_id, buffer.value);
            break;
        case DISCONNECT:
            disconnect(buffer.sender_id);
            break;
        case STOP:
            stop(buffer.sender_id);
            break;
        default:
            printf("Incorrect command.\n");
            break;
    }
}

// zainicjowanie nowego klienta i nadanie ID
void init(key_t client_key) {
    int client_queue = msgget(client_key, IPC_CREAT | 0666);

    struct message buffer;
    buffer.mtype = next_id;
    buffer.value = next_id == MAX_CLIENTS;

    msgsnd(client_queue, &buffer, MESSAGE_SIZE, 0);

    if (next_id < MAX_CLIENTS) {
        printf("Client %d initialized.\n", next_id);

        clients[next_id].queue_key = client_key;
        clients[next_id].queue_id = client_queue;
        clients[next_id].state = CONNECTED;

        next_id++;
    }
    else {
        printf("Failed to initialize client.\n");
    }
}

// wyslanie do klienta listy aktywnych klientow
void list(int client_id) {
    printf("Listing clients for %d.\n", client_id);

    char temp[25];
    struct message buffer;
    buffer.mtype = client_id;
    buffer.mtext[0] = '\0';

    for (int i = 1; i < next_id; i++) {
        if (clients[i].state != DISCONNECTED && i != client_id) {
            sprintf(temp, "%d %s\n", i, clients[i].state == CONNECTED ? "[available]" : "[unavailable]");
            strcat(buffer.mtext, temp);
        }
    }

    msgsnd(clients[client_id].queue_id, &buffer, MESSAGE_SIZE, 0);
}

// sprawdzenie czy klient o danym ID istnieje i jest dostepny
int is_available(int id) {
    return id >= 1 && id < next_id && clients[id].state == CONNECTED;
}

// obsluga polaczenia dwoch klientow
void connect(int client_id, int connect_to_id) {
    int send = client_id != connect_to_id && is_available(client_id) && is_available(connect_to_id);

    if (send) {
        printf("Connecting clients %d and %d.\n", client_id, connect_to_id);

        clients[connect_to_id].state = CHATTING;
        clients[client_id].state = CHATTING;
    }
    else {
        printf("Connection failed.\n");
    }

    struct message buffer;
    buffer.mtype = client_id;
    buffer.value = send ? clients[connect_to_id].queue_key : -1;
    msgsnd(clients[client_id].queue_id, &buffer, MESSAGE_SIZE, 0);

    if (send) {
        buffer.mtype = connect_to_id;
        buffer.value = clients[client_id].queue_key;
        msgsnd(clients[connect_to_id].queue_id, &buffer, MESSAGE_SIZE, 0);
    }
}

// przywrocenie gotowosci klienta do polaczenia
void disconnect(int client_id) {
    printf("Client %d ready to connect.\n", client_id);
    clients[client_id].state = CONNECTED;
}

// rozlaczenie klienta
void stop(int client_id) {
    printf("Client %d disconnected.\n", client_id);
    clients[client_id].state = DISCONNECTED;
}

// obsluga wyjscia z serwera
void exit_handler() {
    printf("Server exit.\n");

    struct message buffer;
    buffer.value = -1;

    // wyslanie do klientow komunikatu o zakonczeniu pracy serwera
    for (int i = 1; i < next_id; i++) {
        buffer.mtype = i;

        if (clients[i].state != DISCONNECTED)
            msgsnd(clients[i].queue_id, &buffer, MESSAGE_SIZE, 0);
    }
    
    // oczekiwanie na rozlaczenie wszystkich klientow
    while (!all_disconnected())
        messages_handler();

    // usuniecie kolejki
    msgctl(queue_id, IPC_RMID, NULL);
}

// sprawdzenie czy wszyscy klienci sa rozlaczeni
int all_disconnected() {
    for (int i = 1; i < next_id; i++) {
        if (clients[i].state != DISCONNECTED)
            return 0;
    }

    return 1;
}

// obsluga sygnalu SIGINT
void sigint_handler(int signal) {
    exit(1);
}
