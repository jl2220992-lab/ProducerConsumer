
#ifndef SHARED_H
#define SHARED_H

#include <windows.h>

#define BUFFER_SIZE 6
#define DATA_LENGTH 10
#define DATA_SIZE (DATA_LENGTH + 1)

#define PRODUCER_COUNT 2
#define CONSUMER_COUNT 3
#define PRODUCE_TIMES 12
#define CONSUME_TIMES 8

#define SHM_NAME "Local\\PC_SharedBuffer"
#define EMPTY_SEM_NAME "Local\\PC_EmptySemaphore"
#define FULL_SEM_NAME "Local\\PC_FullSemaphore"
#define MUTEX_NAME "Local\\PC_BufferMutex"

typedef struct {
    char data[DATA_SIZE];
    BOOL occupied;
} BufferItem;

typedef struct {
    BufferItem buffer[BUFFER_SIZE];
    int in;
    int out;
    int count;
} SharedBuffer;

#endif
