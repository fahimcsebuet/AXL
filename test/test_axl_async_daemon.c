/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411039.
 * All rights reserved.
 * KVTree was originally part of The Scalable Checkpoint / Restart (SCR) library.
 * For details, see https://sourceforge.net/projects/scalablecr/
 * Please also read this file: LICENSE.TXT.
*/

#include "test_axl.h"
#include "test_axl_async_daemon.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define TEST_PASS (0)
#define TEST_FAIL (1)

#define TEST_FILE "test_file"
#define TEST_STRING "I am a file"

#define TEST_NAME "test transfer"
#define TEST_DEST "test_file_moved"

int test_axl_async_daemon(){
    int rc = TEST_PASS;

    /* Create a file */
    unlink(TEST_FILE);
    FILE * fp = fopen(TEST_FILE, "w");
    fputs(TEST_STRING, fp);
    fclose(fp);

    /* Create dest path */
    char pwd[PATH_MAX];
    if (getcwd(pwd, sizeof(pwd)) == NULL) {
        printf("getcwd() failed: errno=%d (%s)\n", errno, strerror(errno));
    }
    char* dest_path = malloc(strlen(pwd) + strlen(TEST_DEST) + 2);
    strcpy(dest_path, pwd);
    strcat(dest_path, "/");
    strcat(dest_path, TEST_DEST);
    unlink(dest_path);

    /* Launch axl, reate a transfer, add test file, dispatch */
    if (AXL_Init(NULL) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    int id = AXL_Create(AXL_XFER_ASYNC_DAEMON, TEST_NAME);
    if (id < 0) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    if (AXL_Add(id, TEST_FILE, dest_path) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    if (AXL_Dispatch(id) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

#if 0
    if (AXL_Cancel(id) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    /* will return AXL_SUCCESS if not cancelled,
     * and !AXL_SUCCESS if cancelled (or error) */
    AXL_Wait(id);
#else
    /* Wait for transfer to complete and finalize axl */
    if (AXL_Wait(id) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }
#endif

    if (AXL_Free(id) != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    if (AXL_Finalize() != AXL_SUCCESS) {
        rc = TEST_FAIL;
        goto cleanup;
    }

    /* Check that file arrived properly */
    FILE* dfp = fopen(dest_path, "r");
    if(!dfp) {
        rc = TEST_FAIL;
    } else {
        char* read_str = malloc(strlen(TEST_STRING) + 1);
        if (fgets(read_str, strlen(TEST_STRING) + 1, dfp) == NULL) {
            printf("fgets() returned NULL\n");
        }
        if(strcmp(read_str, TEST_STRING)) rc = TEST_FAIL;
        free(read_str);
        fclose(dfp);
    }

cleanup:
    /* Unlink test files and return rc */
    unlink(TEST_FILE);
    unlink(dest_path);
    free(dest_path);
    return rc;
}

void test_axl_async_daemon_init(){
    register_test(test_axl_async_daemon, "test_axl_async_daemon");
}
