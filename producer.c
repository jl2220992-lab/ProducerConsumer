
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>

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

    int producerId;
    int i;
    int position;
    int result = 1;

    DWORD lockResult;
    SYSTEMTIME now;

    if (argc != 2) {
        puts("Producer startup argument error.");
        return 1;
    }

    producerId = atoi(argv[1]);

    if (producerId < 1 || producerId > PRODUCER_COUNT) {
        puts("Invalid producer ID.");
        return 1;
    }

    mapHandle = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME
    );

    if (mapHandle == NULL) {
        printf("Producer %d: cannot open shared memory.", producerId);
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

        printf("Producer %d: resource opening failed. Error: %lu",
            producerId, GetLastError());
        putchar(10);
        goto cleanup;
    }

    srand((unsigned int)(
        GetTickCount() ^ GetCurrentProcessId() ^ producerId
    ));

    for (i = 1; i <= PRODUCE_TIMES; i++) {

        if (i == 1) {
            Sleep(1200 + rand() % 400);
        }
        else {
            Sleep(100 + rand() % 250);
        }

        DWORD slotResult = WaitForSingleObject(emptySem, 0);

        if (slotResult == WAIT_TIMEOUT) {

            show_wait_message(
                "Producer",
                producerId,
                "BUFFER FULL - waiting for an empty slot"
            );

            slotResult = WaitForSingleObject(emptySem, INFINITE);
        }

        if (slotResult != WAIT_OBJECT_0) {

            printf(
                "Producer %d: waiting for empty slot failed.",
                producerId
            );

            putchar(10);
            goto cleanup;
        }

        lockResult = WaitForSingleObject(bufferMutex, INFINITE);

        if (lockResult != WAIT_OBJECT_0 &&
            lockResult != WAIT_ABANDONED) {

            ReleaseSemaphore(emptySem, 1, NULL);

            printf("Producer %d: mutex acquisition failed.", producerId);
            putchar(10);
            goto cleanup;
        }

        position = pool->in;

        if (pool->buffer[position].occupied) {

            printf("Producer %d: buffer state error.", producerId);
            putchar(10);

            ReleaseMutex(bufferMutex);
            ReleaseSemaphore(emptySem, 1, NULL);
            goto cleanup;
        }

        sprintf_s(
            pool->buffer[position].data,
            DATA_SIZE,
            "P%d_%07d",
            producerId,
            i
        );

        pool->buffer[position].occupied = TRUE;

        pool->in = (pool->in + 1) % BUFFER_SIZE;
        pool->count++;

        GetLocalTime(&now);

        printf(
            "[%02d:%02d:%02d.%03d] Producer P%d "
            "round %02d: added %s at slot %d",
            (int)now.wHour,
            (int)now.wMinute,
            (int)now.wSecond,
            (int)now.wMilliseconds,
            producerId,
            i,
            pool->buffer[position].data,
            position + 1
        );

        putchar(10);

        print_buffer(pool);
        fflush(stdout);

        ReleaseMutex(bufferMutex);
        ReleaseSemaphore(fullSem, 1, NULL);
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
