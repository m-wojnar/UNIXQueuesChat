#include <stdlib.h>
#include <stdio.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include "config.h"
#include <errno.h>

extern int errno;
// obsluga petli zdarzen
void messages_handler();

// funkcje do obslugi polecen otrzymanych od klientow
void init(char *client_name);
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
    mqd_t queue_id;
    char queue_name[MAX_NAME_SIZE];
};

struct client_info clients[MAX_CLIENTS];
int next_id = 1;
mqd_t queue_id;

int main() {
    // ustawienie obslugi wyjscia z serwera i sygnalu SIGINT
    atexit(exit_handler);
    signal(SIGINT, sigint_handler);

    // utworzenie kolejki serwera
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_BUFFER_SIZE;
    attr.mq_curmsgs = 0;

    queue_id = mq_open(SERVER_NAME, O_CREAT | O_RDONLY, 0644, &attr);

    // petla zdarzen
    while (1) 
        messages_handler();

    return 0;
}

// oczekiwanie na polecenie i wybor funkcji do obslugi polecenia
void messages_handler() {
    char buffer[MAX_BUFFER_SIZE];
    mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);

    switch (buffer[0]) {
        case INIT:
            init(buffer + 1);
            break;
        case LIST:
            list(buffer[1]);
            break;
        case CONNECT:
            connect(buffer[1], buffer[2]);
            break;
        case DISCONNECT:
            disconnect(buffer[1]);
            break;
        case STOP:
            stop(buffer[1]);
            break;
        default:
            printf("Incorrect command.\n");
            break;
    }
}

// zainicjowanie nowego klienta i nadanie ID
void init(char *client_name) {
    int client_queue = mq_open(client_name, O_WRONLY);

    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = next_id;
    buffer[1] = next_id == MAX_CLIENTS ? -1 : 0;

    mq_send(client_queue, buffer, MAX_BUFFER_SIZE, 0);

    if (next_id < MAX_CLIENTS) {
        printf("Client %d initialized.\n", next_id);

        strcpy(clients[next_id].queue_name, client_name);
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
    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = '\0';

    for (int i = 1; i < next_id; i++) {
        if (clients[i].state != DISCONNECTED && i != client_id) {
            sprintf(temp, "%d %s\n", i, clients[i].state == CONNECTED ? "[available]" : "[unavailable]");
            strcat(buffer, temp);
        }
    }

    mq_send(clients[client_id].queue_id, buffer, MAX_BUFFER_SIZE, 0);
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

    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = send;
    strcpy(buffer + 1, clients[connect_to_id].queue_name);
    mq_send(clients[client_id].queue_id, buffer, MAX_BUFFER_SIZE, 0);

    if (send) {
        strcpy(buffer + 1, clients[client_id].queue_name);
        mq_send(clients[connect_to_id].queue_id, buffer, MAX_BUFFER_SIZE, 0);
    }
}

// przywrocenie gotowosci klienta do polaczenia
void disconnect(int client_id) {
    printf("Client %d ready to connect.\n", client_id);
    clients[client_id].state = CONNECTED;
}

// rozlaczenie klienta
void stop(int client_id) {
    mq_close(clients[client_id].queue_id);
    printf("Client %d disconnected.\n", client_id);
    clients[client_id].state = DISCONNECTED;
}

// obsluga wyjscia z serwera
void exit_handler() {
    printf("Server exit.\n");

    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = -1;

    // wyslanie do klientow komunikatu o zakonczeniu pracy serwera
    for (int i = 1; i < next_id; i++) {
        if (clients[i].state != DISCONNECTED) {
            mq_send(clients[i].queue_id, buffer, MAX_BUFFER_SIZE, 0);
            mq_close(clients[i].queue_id);
        }
    }
    
    // oczekiwanie na rozlaczenie wszystkich klientow
    while (!all_disconnected())
        messages_handler();

    // usuniecie kolejki
    mq_close(queue_id);
    mq_unlink(SERVER_NAME);
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
