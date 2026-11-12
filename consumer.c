
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_buffer(const SharedBuffer *pool)
{
    int k;

    printf("Buffer state [%d/%d]:", pool->count, BUFFER_SIZE);

    for (k = 0; k < BUFFER_SIZE; k++) {
        if (pool->buffer[k].occupied) {
            printf(" | %d:%s", k + 1, pool->buffer[k].data);
        }
        else {
            printf(" | %d:EMPTY", k + 1);
        }
    }

    printf(" |");
    putchar(10);
}

static void show_wait_message(
    const char *role,
    int id,
    const char *reason
)
{
    SYSTEMTIME now;
    GetLocalTime(&now);

    printf(
        "[%02u:%02u:%02u.%03u] %s %d: %s\n",
        (unsigned)now.wHour,
        (unsigned)now.wMinute,
        (unsigned)now.wSecond,
        (unsigned)now.wMilliseconds,
        role, id, reason
    );

    fflush(stdout);
}

int main(int argc, char *argv[])
{
    HANDLE mapHandle = NULL;
    HANDLE emptySem = NULL;
    HANDLE fullSem = NULL;
    HANDLE bufferMutex = NULL;

    SharedBuffer *pool = NULL;

    int consumerId;
    int i;
    int position;
    int result = 1;

    char received[DATA_SIZE];

    DWORD lockResult;
    SYSTEMTIME now;

    if (argc != 2) {
        puts("Consumer startup argument error.");
        return 1;
    }

    consumerId = atoi(argv[1]);

    if (consumerId < 1 || consumerId > CONSUMER_COUNT) {
        puts("Invalid consumer ID.");
        return 1;
    }

    /* Connect to the shared resources */
    mapHandle = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME
    );

    if (mapHandle == NULL) {
        printf("Consumer %d: cannot open shared memory.", consumerId);
        putchar(10);
        goto cleanup;
    }

    pool = (SharedBuffer *)MapViewOfFile(
        mapHandle,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        sizeof(SharedBuffer)
    );

    emptySem = OpenSemaphoreA(
        SEMAPHORE_ALL_ACCESS, FALSE, EMPTY_SEM_NAME
    );

    fullSem = OpenSemaphoreA(
        SEMAPHORE_ALL_ACCESS, FALSE, FULL_SEM_NAME
    );

    bufferMutex = OpenMutexA(
        MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME
    );

    if (pool == NULL || emptySem == NULL ||
        fullSem == NULL || bufferMutex == NULL) {

        printf("Consumer %d: resource opening failed. Error: %lu",
            consumerId, GetLastError());
        putchar(10);
        goto cleanup;
    }

    srand((unsigned int)(
        GetTickCount() ^ GetCurrentProcessId() ^ (consumerId * 17)
    ));

    for (i = 1; i <= CONSUME_TIMES; i++) {

        /* Wait for a random period before consuming */
        if (i == 1) {
            Sleep(100 + rand() % 200);
        }
        else {
            Sleep(1200 + rand() % 400);
        }

        /* Wait until at least one item is available */
        DWORD dataResult = WaitForSingleObject(fullSem, 0);

        if (dataResult == WAIT_TIMEOUT) {

            show_wait_message(
                "Consumer",
                consumerId,
                "BUFFER EMPTY - waiting for new data"
            );

            dataResult = WaitForSingleObject(fullSem, INFINITE);
        }

        if (dataResult != WAIT_OBJECT_0) {

            printf(
                "Consumer %d: waiting for data failed.",
                consumerId
            );

            putchar(10);
            goto cleanup;
        }

        lockResult = WaitForSingleObject(bufferMutex, INFINITE);

        if (lockResult != WAIT_OBJECT_0 &&
            lockResult != WAIT_ABANDONED) {

            ReleaseSemaphore(fullSem, 1, NULL);

            printf("Consumer %d: mutex acquisition failed.", consumerId);
            putchar(10);
            goto cleanup;
        }

        position = pool->out;

        if (!pool->buffer[position].occupied) {

            printf("Consumer %d: buffer state error.", consumerId);
            putchar(10);

            ReleaseMutex(bufferMutex);
            ReleaseSemaphore(fullSem, 1, NULL);
            goto cleanup;
        }

        /* Read the item before clearing its slot */
        strcpy_s(
            received,
            sizeof(received),
            pool->buffer[position].data
        );

        pool->buffer[position].occupied = FALSE;
        pool->buffer[position].data[0] = '\0';

        pool->out = (pool->out + 1) % BUFFER_SIZE;
        pool->count--;

        GetLocalTime(&now);

        printf(
            "[%02d:%02d:%02d.%03d] Consumer C%d "
            "round %02d: received %s from slot %d",
            (int)now.wHour,
            (int)now.wMinute,
            (int)now.wSecond,
            (int)now.wMilliseconds,
            consumerId,
            i,
            received,
            position + 1
        );

        putchar(10);

        print_buffer(pool);
        fflush(stdout);

        /* Make the slot available to producers */
        ReleaseMutex(bufferMutex);
        ReleaseSemaphore(emptySem, 1, NULL);
    }

    result = 0;

cleanup:

    if (pool != NULL) {
        UnmapViewOfFile(pool);
    }

    if (bufferMutex != NULL) CloseHandle(bufferMutex);
    if (emptySem != NULL) CloseHandle(emptySem);
    if (fullSem != NULL) CloseHandle(fullSem);
    if (mapHandle != NULL) CloseHandle(mapHandle);

    return result;
}
