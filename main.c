
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>

#define WORKER_TOTAL (PRODUCER_COUNT + CONSUMER_COUNT)

static BOOL launch_worker(
    const char *fileName,
    int workerId,
    STARTUPINFOA *startup,
    PROCESS_INFORMATION *processInfo
)
{
    char fullPath[MAX_PATH];
    char commandLine[MAX_PATH + 64];

    DWORD pathLength;
    DWORD errorCode;
    int written;

    pathLength = GetFullPathNameA(
        fileName,
        MAX_PATH,
        fullPath,
        NULL
    );

    if (pathLength == 0 || pathLength >= MAX_PATH) {
        printf("Cannot locate executable: %s\n", fileName);
        return FALSE;
    }

    written = sprintf_s(
        commandLine,
        sizeof(commandLine),
        "\"%s\" %d",
        fullPath,
        workerId
    );

    if (written < 0) {
        puts("Failed to build process command line.");
        return FALSE;
    }

    ZeroMemory(processInfo, sizeof(PROCESS_INFORMATION));

    if (!CreateProcessA(
        fullPath,
        commandLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        startup,
        processInfo
    )) {

        errorCode = GetLastError();

        printf(
            "Failed to start %s %d. Error: %lu\n",
            fileName,
            workerId,
            errorCode
        );

        return FALSE;
    }

    CloseHandle(processInfo->hThread);
    processInfo->hThread = NULL;

    return TRUE;
}


int main(void)
{
    HANDLE mapping = NULL;
    HANDLE emptySem = NULL;
    HANDLE fullSem = NULL;
    HANDLE bufferMutex = NULL;

    SharedBuffer *pool = NULL;

    PROCESS_INFORMATION workers[WORKER_TOTAL];
    HANDLE waitHandles[WORKER_TOTAL];

    STARTUPINFOA startup;

    int started = 0;
    int i;

    int allEmpty = 1;
    int allWorkersOK = 1;
    int occupiedCount = 0;
    int success = 0;

    DWORD waitResult;
    DWORD exitCode = 0;

    ZeroMemory(workers, sizeof(workers));
    ZeroMemory(&startup, sizeof(startup));

    startup.cb = sizeof(startup);

    puts("========================================");
    puts("     Producer-Consumer Simulation");
    puts("========================================");


    /* Create shared memory */

    SetLastError(ERROR_SUCCESS);

    mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        (DWORD)sizeof(SharedBuffer),
        SHM_NAME
    );

    if (mapping == NULL) {

        printf(
            "Shared memory creation failed: %lu\n",
            GetLastError()
        );

        goto cleanup;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        puts("Shared memory object already exists.");
        goto cleanup;
    }


    /* Map shared memory into current process */

    pool = (SharedBuffer *)MapViewOfFile(
        mapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedBuffer)
    );

    if (pool == NULL) {

        printf(
            "Memory mapping failed: %lu\n",
            GetLastError()
        );

        goto cleanup;
    }


    /* Initialize buffer pool */

    ZeroMemory(pool, sizeof(SharedBuffer));

    for (i = 0; i < BUFFER_SIZE; i++) {
        pool->buffer[i].occupied = FALSE;
        pool->buffer[i].data[0] = '\0';
    }


    /* Create empty-slot semaphore */

    SetLastError(ERROR_SUCCESS);

    emptySem = CreateSemaphoreA(
        NULL,
        BUFFER_SIZE,
        BUFFER_SIZE,
        EMPTY_SEM_NAME
    );

    if (emptySem == NULL) {

        printf(
            "Empty semaphore creation failed: %lu\n",
            GetLastError()
        );

        goto cleanup;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        puts("Empty semaphore already exists.");
        goto cleanup;
    }


    /* Create available-data semaphore */

    SetLastError(ERROR_SUCCESS);

    fullSem = CreateSemaphoreA(
        NULL,
        0,
        BUFFER_SIZE,
        FULL_SEM_NAME
    );

    if (fullSem == NULL) {

        printf(
            "Full semaphore creation failed: %lu\n",
            GetLastError()
        );

        goto cleanup;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        puts("Full semaphore already exists.");
        goto cleanup;
    }


    /* Create mutex */

    SetLastError(ERROR_SUCCESS);

    bufferMutex = CreateMutexA(
        NULL,
        FALSE,
        MUTEX_NAME
    );

    if (bufferMutex == NULL) {

        printf(
            "Mutex creation failed: %lu\n",
            GetLastError()
        );

        goto cleanup;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        puts("Mutex already exists.");
        goto cleanup;
    }


    /* Display initialization information */

    puts("[OK] Shared memory initialized.");
    puts("[OK] Empty semaphore: 6.");
    puts("[OK] Full semaphore: 0.");
    puts("[OK] Mutex initialized.");

    printf(
        "Buffer capacity: %d, producers: %d, consumers: %d\n",
        BUFFER_SIZE,
        PRODUCER_COUNT,
        CONSUMER_COUNT
    );

    puts("");
    puts("Starting worker processes...");
    fflush(stdout);


    /* Start producer processes */

    for (i = 1; i <= PRODUCER_COUNT; i++) {

        if (!launch_worker(
            "producer.exe",
            i,
            &startup,
            &workers[started]
        )) {
            goto abort_workers;
        }

        waitHandles[started] = workers[started].hProcess;
        started++;
    }


    /* Start consumer processes */

    for (i = 1; i <= CONSUMER_COUNT; i++) {

        if (!launch_worker(
            "consumer.exe",
            i,
            &startup,
            &workers[started]
        )) {
            goto abort_workers;
        }

        waitHandles[started] = workers[started].hProcess;
        started++;
    }


    /* Wait for all five processes */

    puts("");
    puts("All five processes have been created.");
    puts("Waiting for all workers to finish...");
    fflush(stdout);

    waitResult = WaitForMultipleObjects(
        WORKER_TOTAL,
        waitHandles,
        TRUE,
        INFINITE
    );

    if (waitResult != WAIT_OBJECT_0) {

        if (waitResult == WAIT_FAILED) {

            printf(
                "Waiting for workers failed: %lu\n",
                GetLastError()
            );
        }
        else {

            printf(
                "Unexpected wait result: %lu\n",
                waitResult
            );
        }

        goto abort_workers;
    }


    /* Check every worker's exit status */

    for (i = 0; i < WORKER_TOTAL; i++) {

        if (!GetExitCodeProcess(
            workers[i].hProcess,
            &exitCode
        )) {

            printf(
                "Worker %d: failed to query exit status. Error: %lu\n",
                i + 1,
                GetLastError()
            );

            allWorkersOK = 0;
            continue;
        }

        if (exitCode != 0) {

            printf(
                "Worker %d failed. Exit code: %lu\n",
                i + 1,
                exitCode
            );

            allWorkersOK = 0;
        }
    }


    /* Final buffer verification */

    puts("");
    puts("========================================");
    puts("           Final Statistics");
    puts("========================================");

    printf(
        "Expected production: %d x %d = %d\n",
        PRODUCER_COUNT,
        PRODUCE_TIMES,
        PRODUCER_COUNT * PRODUCE_TIMES
    );

    printf(
        "Expected consumption: %d x %d = %d\n",
        CONSUMER_COUNT,
        CONSUME_TIMES,
        CONSUMER_COUNT * CONSUME_TIMES
    );

    printf("Final buffer count: %d\n", pool->count);

    for (i = 0; i < BUFFER_SIZE; i++) {

        if (pool->buffer[i].occupied) {

            allEmpty = 0;
            occupiedCount++;
        }

        printf(
            "Slot %d: %s\n",
            i + 1,
            pool->buffer[i].occupied ?
                pool->buffer[i].data : "EMPTY"
        );
    }


    /* Verify final result */

    if (
        allWorkersOK &&
        allEmpty &&
        pool->count == 0 &&
        occupiedCount == 0
    ) {

        puts("");
        puts("[PASS] All processes finished successfully.");
        puts("[PASS] The buffer pool is empty.");

        success = 1;
    }
    else {

        puts("");
        puts("[FAIL] Final verification failed.");

        printf(
            "Worker status: %s\n",
            allWorkersOK ? "OK" : "ERROR"
        );

        printf("Recorded buffer count: %d\n", pool->count);
        printf("Occupied slots: %d\n", occupiedCount);
    }

    goto cleanup;


    /* Handle partial process startup failure */

abort_workers:

    for (i = 0; i < started; i++) {

        if (workers[i].hProcess != NULL) {
            TerminateProcess(workers[i].hProcess, 1);
        }
    }

    for (i = 0; i < started; i++) {

        if (workers[i].hProcess != NULL) {
            WaitForSingleObject(
                workers[i].hProcess,
                INFINITE
            );
        }
    }


    /* Release all resources */

cleanup:

    for (i = 0; i < started; i++) {

        if (workers[i].hProcess != NULL) {
            CloseHandle(workers[i].hProcess);
        }
    }

    if (pool != NULL) {
        UnmapViewOfFile(pool);
    }

    if (bufferMutex != NULL) {
        CloseHandle(bufferMutex);
    }

    if (fullSem != NULL) {
        CloseHandle(fullSem);
    }

    if (emptySem != NULL) {
        CloseHandle(emptySem);
    }

    if (mapping != NULL) {
        CloseHandle(mapping);
    }

    return success ? 0 : 1;
}
