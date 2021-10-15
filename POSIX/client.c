#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
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
void chat(mqd_t other_queue_id);
int handle_chat_messages(char *buffer);
int handle_chat_input(char *buffer, int other_queue_id);

// obsluga zamkniecia klienta i sygnalu SIGINT
void exit_handler();
void sigint_handler(int signal);

mqd_t server_queue_id;
mqd_t queue_id;
int client_id;
char client_name[MAX_NAME_SIZE];

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
    server_queue_id = mq_open(SERVER_NAME, O_CREAT | O_WRONLY);
    
    struct mq_attr attr;
    attr.mq_flags = O_NONBLOCK;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_BUFFER_SIZE;
    attr.mq_curmsgs = 0;
    
    sprintf(client_name, "/%d_queue", getpid());
    queue_id = mq_open(client_name, O_CREAT | O_NONBLOCK | O_RDONLY, 0644, &attr);

    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = INIT;
    strcpy(buffer + 1, client_name);
    mq_send(server_queue_id, buffer, MAX_BUFFER_SIZE, INIT);
    
    do {
        errno = 0;
        mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);
    } while (errno == EAGAIN);

    client_id = buffer[0];
    if (buffer[1] != 0) {
        printf("Cannot start new client.\n");
        exit(-1);
    }
}

// obsluga komunikatow z kolejki zdarzen klienta
void handle_messages() {
    char buffer[MAX_BUFFER_SIZE];
    mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);

    if (errno != EAGAIN) {
        if (buffer[0] == -1) {
            exit(1);
        }
        else {
            chat(mq_open(buffer + 1, O_CREAT | O_WRONLY));
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
    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = LIST;
    buffer[1] = client_id;

    mq_send(server_queue_id, buffer, MAX_BUFFER_SIZE, LIST);

    do {
        errno = 0;
        mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);
    } while (errno == EAGAIN);

    char *line = strtok(buffer, "\n");
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
    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = CONNECT;
    buffer[1] = client_id;
    buffer[2] = connect_to_id;

    mq_send(server_queue_id, buffer, MAX_BUFFER_SIZE, CONNECT);

    do {
        errno = 0;
        mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);
    } while (errno == EAGAIN);

    if (!buffer[0]) {
        printf("Connection to %d failed.\n", connect_to_id);
        return -1;
    }

    return mq_open(buffer + 1, O_CREAT | O_WRONLY, 0666);
}

// petla zdarzen czatu
void chat(mqd_t other_queue_id) {
    printf("\nEntering chat. Type '!' to exit.\n");
    fflush(stdin);

    char buffer[MAX_BUFFER_SIZE];

    while (1) {
        if (handle_chat_messages(buffer) == -1)
            break;

        if (handle_chat_input(buffer, other_queue_id) == -1)
            break;
    }

    mq_close(other_queue_id);
    printf("Chat ended.\n");
}

// obsluga nadchodzacych wiadomosci w czacie
int handle_chat_messages(char *buffer) {
    mq_receive(queue_id, buffer, MAX_BUFFER_SIZE, NULL);

    if (buffer[0] == -1)
        exit(1);

    if (errno != EAGAIN) {
        if (buffer[1] == '!')
            return -1;

        printf(" << %s\n", buffer + 1);
    }

    errno = 0;
    return 0;
}

// obsluga wysylania wiadomosci w czacie
int handle_chat_input(char *buffer, int other_queue_id) {
    if (is_input_available()) {
        int input, i = 1;

        while ((input = fgetc(stdin)) != '\n')
            buffer[i++] = (char) input;
        
        buffer[i] = '\0';
        mq_send(other_queue_id, buffer, MAX_BUFFER_SIZE, 0);

        if (buffer[1] == '!')
            return -1;
    }

    return 0;
}

// wyslanie informacji do serwera o gotowosci do polaczenia
void disconnect() {
    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = DISCONNECT;
    buffer[1] = client_id;

    mq_send(server_queue_id, buffer, MAX_BUFFER_SIZE, DISCONNECT);
}

// wyslanie informacji o rozlaczeniu z serwerem i usuniecie kolejki
void exit_handler() {
    char buffer[MAX_BUFFER_SIZE];
    buffer[0] = STOP;
    buffer[1] = client_id;

    mq_send(server_queue_id, buffer, MAX_BUFFER_SIZE, DISCONNECT);
    mq_close(server_queue_id);
    mq_close(queue_id);
    mq_unlink(client_name);
}

// obsluga sygnalu SIGINT
void sigint_handler(int signal) {
    exit(1);
}
