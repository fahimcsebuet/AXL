/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411039.
 * All rights reserved.
 * This file is part of The Scalable Checkpoint / Restart (SCR) library.
 * For details, see https://sourceforge.net/projects/scalablecr/
 * Please also read this file: LICENSE.TXT.
*/

// dirname and basename
#include <libgen.h>

// uLong type for CRCs
#include <zlib.h>

#ifdef HAVE_CPPR
#include "cppr.h"
#endif /* HAVE_CPPR */

typedef enum {
    AXL_XFER_SYNC,
    AXL_XFER_ASYNC_DAEMON,
    AXL_XFER_ASYNC_DW,
    AXL_XFER_ASYNC_BBAPI,
    AXL_XFER_ASYNC_CPPR,
} axl_xfer_t;

/*
=========================================
Global Variables
========================================
*/

/* AXL's flush file, SCR has one as well */
char* axl_flush_file = NULL;

/* transfer file */
char* axl_transfer_file = NULL;

/* Transfer handle unique IDs */
static int axl_next_handle_UID = -1;

/* tracks list of files written with flush */
static kvtree* axl_flush_async_file_lists = NULL;

/*
=========================================
Helper Functions
========================================
*/

axl_xfer_t axl_parse_type_string (char* type) {
    return SYNC;
}

/*
=========================================
API Functions
========================================
*/

/* Read configuration from non-AXL-specific file
  Also, start up vendor specific services */
int AXL_Init (char* conf_file) {

    axl_next_handle_UID = 0;
    axl_flush_async_file_lists = kvtree_new();

    char* axl_cntl_dir;
    axl_read_config(axl_cntl_dir);

    char* axl_flush_file_name = "/axl_flush.info";
    axl_flush_file = malloc(strlen(axl_cntl_dir) + strlen(axl_flush_file_name));
    strcpy(axl_flush_file, axl_cntl_dir);
    strcat(axl_flush_file, axl_flush_file_name);

#ifdef HAVE_DAEMON
    /* daemon stuff */
    char* axl_transfer_file_name = "/axl_transfer.info";
    axl_transfer_file = malloc(strlen(axl_cntl_dir) + strlen(axl_transfer_file_name));
    strcpy(axl_transfer_file, axl_cntl_dir);
    strcat(axl_transfer_file, axl_transfer_file_name);

    axl_free(&axl_cntl_dir);

    /* wait until transfer daemon is stopped */
    axl_flush_async_stop();

    /* clear out the file */
    /* done by all ranks (to avoid mpi dependency)
     * Could go back to one/node (or other storage desc as appropriate
     */
    axl_file_unlink(axl_transfer_file);
#endif

#ifdef HAVE_LIBCPPR
    /* attempt to init cppr */
    int cppr_ret = cppr_status();
    if (cppr_ret != CPPR_SUCCESS) {
        axl_abort(-1, "libcppr cppr_status() failed: %d '%s' @ %s:%d",
                  cppr_ret, cppr_err_to_str(cppr_ret), __FILE__, __LINE__
                  );
    }
    axl_dbg(1, "#bold CPPR is present @ %s:%d", __FILE__, __LINE__);
#endif /* HAVE_LIBCPPR */

#ifdef HAVE_BBAPI
    // TODO: BBAPI wants MPI rank information here?
    int rank = 0;
    int bbapi_ret = BB_InitLibrary(rank, BBAPI_CLIENTVERSIONSTR);
    if (bbapi_ret != 0) {
        axl_abort(-1, "BBAPI Failed to initialize");
    }
#endif

  return AXL_SUCCESS;
}

/* Shutdown any vendor services */
int AXL_Finalize (void) {
#ifdef HAVE_DAEMON
    axl_free(&axl_transfer_file);
#endif
    axl_file_unlink(axl_flush_file);

#ifdef HAVE_LIBCPPR

#endif

#ifdef HAVE_BBAPI
    int rc = BB_TerminateLibrary();
#endif

    return AXL_SUCCESS;
}

/* Create a transfer handle (used for 0+ files)
 * Type specifies a particular method to use
 * Name is a user/application provided string
 * Returns an ID to the transfer handle */
int AXL_Create (char* type, const char* name) {

    /* Parse type of transfer */
    axl_xfer_t xtype = axl_parse_type_string(type);

    /* Generate next unique ID */
    int cur_id = ++axl_next_handle_UID;

    /* Create an entry for this transfer handle
     * record user string and transfer type */
    kvtree* file_list = kvtree_util_set_kv_int(axl_flush_async_file_lists, AXL_KEY_HANDLE_UID, cur_id);
    kvtree_util_set_str(file_list, AXL_KEY_UNAME, name);
    kvtree_util_set_str(file_list, AXL_KEY_XFER_TYPE_STR, type);
    kvtree_util_set_int(file_list, AXL_KEY_XFER_TYPE_INT, xtype);
    kvtree_util_set_int(file_list, AXL_KEY_FLUSH_STATUS, AXL_FLUSH_STATUS_SOURCE);

    switch (xtype) {
    case AXL_XFER_SYNC:
        break;
    case AXL_XFER_ASYNC_DAEMON:
        break;
    case AXL_XFER_ASYNC_DW:
        break;
    case AXL_XFER_ASYNC_BBAPI:
        axl_flush_async_create_bbapi(id);
        break;
    case AXL_XFER_ASYNC_CPPR:
        break;
    }

    return cur_id;
}

/* Add a file to an existing transfer handle */
int AXL_Add (int id, char* source, char* destination) {
    kvtree* file_list = kvtree_get_kv_int(axl_flush_async_file_lists, AXL_HANDLE_UID, id);
    if (file_list == NULL) {
        axl_err("AXL_Add failed: could not find fileset for UID %d", id);
        return AXL_FAILURE;
    }

    axl_xfer_t xtype;
    kvtree_util_get_int(file_list, AXL_KEY_XFER_TYPE_INT, &xtype);

    kvtree_setf(file_list, NULL, "%s %s %s %s", AXL_KEY_FILES, source, AXL_KEY_FILE_DEST, destination);
    kvtree_setf(file_list, NULL, "%s %s %s %d", AXL_KEY_FILES, source, AXL_KEY_FLUSH_STATUS, AXL_FLUSH_STATUS_SOURCE);

    switch (xtype) {
    case AXL_XFER_SYNC:
        break;
    case AXL_XFER_ASYNC_DAEMON:
        break;
    case AXL_XFER_ASYNC_DW:
        break;
    case AXL_XFER_ASYNC_BBAPI:
        return axl_flush_async_add_file_bbapi(id, source, destination);
    case AXL_XFER_ASYNC_CPPR:
        break;
    }

    return AXL_SUCCESS;
}

/* Initiate a transfer for all files in handle ID */
int AXL_Dispatch (int id) {

    kvtree* file_list = kvtree_get_kv_int(axl_flush_async_file_lists, AXL_KEY_HANDLE_UID, id);
    if (file_list == NULL) {
        axl_err("AXL_Dispatch failed: could not find fileset for UID %d", id);
        return AXL_FAILURE;
    }

    axl_xfer_t xtype;
    kvtree_util_get_int(file_list, AXL_KEY_XFER_TYPE_INT, &xtype);

    kvtree_elem* elem;
    kvtree* files = kvtree_get(file_list, AXL_KEY_FILES);
    for (elem = kvtree_elem_first(files); elem != NULL; elem = kvtree_elem_next(elem)) {
        char* source = kvtree_elem_key(elem);

        /* destination for a file */
        kvtree* elem_hash = kvtree_elem_hash(elem);
        char* destination;
        kvtree_util_get_str(elem_hash, AXL_KEY_FILE_DEST, &destination);

        /* figure out and create dirs that should exist */
        /* TODO: vendors may implement smarter functions for mkdir */
        char* dest_path = strdup(destination);
        dirname(dest_path);
        mkdir(dest_path);
        free(dest_path);

        /* calculate CRCs for each file */
        uLong* crc;
        int rc = kvtree_util_get_crc32(elem_hash, AXL_KEY_FILE_CRC, &crc);
        if (rc != KVTREE_SUCCESS) {
            axl_crc32(source, crc);
            kvtree_util_set_crc32(elem_hash, AXL_KEY_FILE_CRC, crc);
        }

    }

    /* NOTE FOR XFER INTERFACES
     * each interface should update AXL_KEY_FLUSH_STATUS
     * all well as AXL_KEY_FILE_STATUS for each file */
    switch (xtype) {
    case AXL_XFER_SYNC:
        return axl_flush_file_list(id);
    case AXL_XFER_ASYNC_DAEMON:
        return axl_flush_async_start_daemon(id);
    case AXL_XFER_ASYNC_DW:
        return axl_flush_async_start_datawarp(id);
    case AXL_XFER_ASYNC_BBAPI:
        return axl_flush_async_start_bbapi(id);
    case AXL_XFER_ASYNC_CPPR:
        return axl_flush_async_start_cppr(id);
    }

    return AXL_SUCCESS;
}

/* Test if a transfer has completed
 * Returns 1 if the transfer has completed
 * Returns <0 if there is an error */
int AXL_Test(int id) {

    kvtree* file_list = kvtree_get_kv_int(axl_flush_async_file_lists, AXL_KEY_HANDLE_UID, id);
    if (file_list == NULL) {
        axl_err("AXL_Test failed: could not find fileset UID=%d", id);
        return AXL_FAILURE;
    }

    axl_xfer_t xtype;
    kvtree_util_get_int(file_list, AXL_KEY_XFER_TYPE_INT, &xtype);

    int status;
    kvtree_util_set_int(file_list, AXL_KEY_FLUSH_STATUS, &status);
    if (status == AXL_STATUS_DEST) {
        return 1;
    } else if (status == AXL_STATUS_ERROR) {
        return AXL_FAILURE;
    } else if (status == AXL_STATUS_SOURCE) {
        axl_err("AXL_Test failed: testing a transfer which was never started UID=%d", id);
        return AXL_FAILURE;
    } // else (status == AXL_STATUS_INPROG) send to XFER interfaces

    switch (xtype) {
    case AXL_XFER_SYNC:
        // error: sync should never be inprogress
        return AXL_FAILURE;
    case AXL_XFER_ASYNC_DAEMON:
        return axl_flush_async_test_daemon(id);
    case AXL_XFER_ASYNC_DW:
        return axl_flush_async_test_datawarp(id);
    case AXL_XFER_ASYNC_BBAPI:
        return axl_flush_async_test_bbapi(id);
    case AXL_XFER_ASYNC_CPPR:
        return axl_flush_async_test_cppr(id);
    }
}

/* BLOCKING
 * Wait for a transfer to complete */
int AXL_Wait (int id) {
    axl_abort("ABORT ABORT SYNC DIDNT WORK");
    return AXL_SUCCESS;
}

/* Cancel an existing transfer */
// TODO: Does cancel call free?
int AXL_Cancel (int id) {
    return AXL_SUCCESS;
}

/* Perform cleanup of internal data associated with ID */
int AXL_Free (int id) {
    return AXL_SUCCESS;
}
