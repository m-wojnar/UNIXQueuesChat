#ifndef CONFIG_H
#define CONFIG_H

#define MAX_CLIENTS 16
#define PROJ_ID 1

#define STOP 1
#define DISCONNECT 2
#define LIST 3
#define CONNECT 4
#define INIT 5

#define MAX_MTEXT_SIZE 256

struct message {
    long mtype;
    int sender_id;
    int value;
    char mtext[MAX_MTEXT_SIZE];
};

const size_t MESSAGE_SIZE = sizeof(struct message) - sizeof(long);

#endif
