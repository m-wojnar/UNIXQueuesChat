#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "config.h"

extern int errno;

// funkcje do ustawienia polaczen oraz obslugi petli zdarzen
void setup_connection();
void handle_messages();
int is_input_available();
void handle_input();

// funkcje obslugujace komendy
void run_command(char command);
void list();
void disconnect();

// polaczenie z innym klientem i obsluga czatu
void run_chat();
int connect(int connect_to_id);
void chat(int other_queue_id);
int handle_chat_messages(struct message *buffer);
int handle_chat_input(struct message *buffer, int other_queue_id);

// obsluga zamkniecia klienta i sygnalu SIGINT
void exit_handler();
void sigint_handler(int signal);

int server_queue_id;
int queue_id;
int client_id;

int main() {
    // ustawienie obslugi wyjscia z klienta i sygnalu SIGINT
    atexit(exit_handler);
    signal(SIGINT, sigint_handler);

    setup_connection();

    // wypisanie informacji o kliencie i listy komend
    printf("My ID is %d\n\n", client_id);
    printf("Available commands:\n - L (list other clients),\n - C id (connect to client),\n - S (stop).\n\n> ");
    fflush(stdout);

    // petla zdarzen
    while (1) {
        handle_messages();
        handle_input();
    }

    return 0;
}

// polaczenie z serwerem i odebranie ID klienta
void setup_connection() {
    key_t key = ftok(getenv("HOME"), getpid());
    queue_id = msgget(key, IPC_CREAT | 0666);

    key_t server_key = ftok(getenv("HOME"), PROJ_ID);
    server_queue_id = msgget(server_key, IPC_CREAT | 0666);

    struct message buffer;
    buffer.value = key;
    buffer.mtype = INIT;
    msgsnd(server_queue_id, &buffer, MESSAGE_SIZE, 0);

    msgrcv(queue_id, &buffer, MESSAGE_SIZE, 0, 0);
    client_id = buffer.mtype;

    if (buffer.value != 0) {
        printf("Cannot start new client.\n");
        exit(-1);
    }
}

// obsluga komunikatow z kolejki zdarzen klienta
void handle_messages() {
    struct message buffer;
    msgrcv(queue_id, &buffer, MESSAGE_SIZE, 0, IPC_NOWAIT);

    if (errno != ENOMSG) {
        if (buffer.value == -1) {
            exit(1);
        }
        else {
            chat(msgget(buffer.value, IPC_CREAT | 0666));
            disconnect();

            printf("> ");
            fflush(stdout);
        }
    }
        
    errno = 0;
}

// obsluga komend ze standardowego wejscia
void handle_input() {
    char command[10];

    if (is_input_available()) {
        scanf("%s", command);
        run_command(command[0]);

        printf("> ");
        fflush(stdout);
    }
}

// sprawdzenie co 0.1 [s] czy na wejsciu podano jakis tekst
int is_input_available() {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

// wybor komendy
void run_command(char command) {
    if (command == 'L' || command == 'l')
        list();
    else if (command == 'S' || command == 's') 
        exit(0);
    else if (command == 'C' || command == 'c') 
        run_chat();
    else 
        printf("Incorrect command.\n");
}

// wyslanie prosby o liste aktywnych klientow i wypisanie na ekran
void list() {
    struct message buffer;
    buffer.sender_id = client_id;
    buffer.mtype = LIST;

    msgsnd(server_queue_id, &buffer, MESSAGE_SIZE, 0);
    msgrcv(queue_id, &buffer, MESSAGE_SIZE, 0, 0);

    char *line = strtok(buffer.mtext, "\n");
    while (line != NULL) {
        printf("Client ID: %s\n", line);
        line = strtok(NULL, "\n");
    }
}

// wyslanie prosby o polaczenie z innym klientem
void run_chat() {
    char num[15], *endptr;
    scanf("%s", num);
    fgetc(stdin);

    int other_id = strtol(num, &endptr, 10);
    if (endptr == num) {
        printf("Incorrect ID.\n");
        return;
    }

    int other_queue_id = connect(other_id);
    if (other_queue_id != -1) {
        chat(other_queue_id);
        disconnect();
    }
}

// polaczenie z innym klientem
int connect(int connect_to_id) {
    struct message buffer;
    buffer.sender_id = client_id;
    buffer.value = connect_to_id;
    buffer.mtype = CONNECT;

    msgsnd(server_queue_id, &buffer, MESSAGE_SIZE, 0);
    msgrcv(queue_id, &buffer, MESSAGE_SIZE, 0, 0);

    if (buffer.value == -1) {
        printf("Connection to %d failed.\n", connect_to_id);
        return -1;
    }

    return msgget(buffer.value, IPC_CREAT | 0666);
}

// petla zdarzen czatu
void chat(int other_queue_id) {
    printf("\nEntering chat. Type '!' to exit.\n");
    fflush(stdin);

    struct message buffer;
    buffer.mtype = client_id;
    buffer.value = 0;

    while (1) {
        if (handle_chat_messages(&buffer) == -1)
            break;

        if (handle_chat_input(&buffer, other_queue_id) == -1)
            break;
    }

    printf("Chat ended.\n");
}

// obsluga nadchodzacych wiadomosci w czacie
int handle_chat_messages(struct message *buffer) {
    msgrcv(queue_id, buffer, MESSAGE_SIZE, 0, IPC_NOWAIT);

    if (buffer->value == -1)
        exit(1);

    if (errno != ENOMSG) {
        if (buffer->mtext[0] == '!')
            return -1;

        printf(" << %s\n", buffer->mtext);
    }

    errno = 0;
    return 0;
}

// obsluga wysylania wiadomosci w czacie
int handle_chat_input(struct message *buffer, int other_queue_id) {
    if (is_input_available()) {
        int input, i = 0;

        while ((input = fgetc(stdin)) != '\n')
            buffer->mtext[i++] = (char) input;
        
        buffer->mtext[i] = '\0';
        msgsnd(other_queue_id, buffer, MESSAGE_SIZE, 0);

        if (buffer->mtext[0] == '!')
            return -1;
    }

    return 0;
}

// wyslanie informacji do serwera o gotowosci do polaczenia
void disconnect() {
    struct message buffer;
    buffer.sender_id = client_id;
    buffer.mtype = DISCONNECT;

    msgsnd(server_queue_id, &buffer, MESSAGE_SIZE, 0);
}

// wyslanie informacji o rozlaczeniu z serwerem i usuniecie kolejki
void exit_handler() {
    struct message buffer;
    buffer.sender_id = client_id;
    buffer.mtype = STOP;

    msgsnd(server_queue_id, &buffer, MESSAGE_SIZE, 0);
    msgctl(queue_id, IPC_RMID, NULL);
}

// obsluga sygnalu SIGINT
void sigint_handler(int signal) {
    exit(1);
}
