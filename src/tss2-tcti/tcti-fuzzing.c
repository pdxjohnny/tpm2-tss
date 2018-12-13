/* SPDX-License-Identifier: BSD-2 */
/*
 * Copyright (c) 2018 Intel Corporation
 * All rights reserved.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "tss2_mu.h"
#include "tss2_tcti_fuzzing.h"

#include "tcti-fuzzing.h"
#include "tcti-common.h"
#include "util/key-value-parse.h"
#define LOGMODULE tcti
#include "util/log.h"

int
fuzz_fill (
        TSS2_SYS_CONTEXT *sysContext,
        size_t count,
        ...)
{
    va_list ap;
    const uint8_t *data = NULL;
    const uint8_t *curr = NULL;
    size_t size = 0U;
    size_t i = 0U;
    void *dest;
    size_t length = 0U;
    size_t combined = 0U;
    _TSS2_SYS_CONTEXT_BLOB *ctx = NULL;
    TSS2_TCTI_FUZZING_CONTEXT *tcti_fuzzing = NULL;

    ctx = syscontext_cast (sysContext);
    tcti_fuzzing = tcti_fuzzing_context_cast (ctx->tctiContext);
    data = tcti_fuzzing->data;
    size = tcti_fuzzing->size;

    va_start (ap, count);

    for (i = 0U; i < (count / 2); ++i) {
        length = va_arg (ap, size_t);
        dest = va_arg (ap, void *);
        if (size > (combined + length)) {
            curr = &data[combined];
            combined += length;
            memcpy (dest, curr, length);
        }
    }

    va_end (ap);

    return EXIT_SUCCESS;
}

/*
 * This function wraps the "up-cast" of the opaque TCTI context type to the
 * type for the fuzzing TCTI context. The only safeguard we have to ensure this
 * operation is possible is the magic number in the fuzzing TCTI context.
 * If passed a NULL context, or the magic number check fails, this function
 * will return NULL.
 */
TSS2_TCTI_FUZZING_CONTEXT*
tcti_fuzzing_context_cast (TSS2_TCTI_CONTEXT *tcti_ctx)
{
    if (tcti_ctx != NULL && TSS2_TCTI_MAGIC (tcti_ctx) == TCTI_FUZZING_MAGIC) {
        return (TSS2_TCTI_FUZZING_CONTEXT*)tcti_ctx;
    }
    return NULL;
}

/*
 * This function down-casts the fuzzing TCTI context to the common context
 * defined in the tcti-common module.
 */
TSS2_TCTI_COMMON_CONTEXT*
tcti_fuzzing_down_cast (TSS2_TCTI_FUZZING_CONTEXT *tcti_fuzzing)
{
    if (tcti_fuzzing == NULL) {
        return NULL;
    }
    return &tcti_fuzzing->common;
}

TSS2_RC
tcti_fuzzing_transmit (
    TSS2_TCTI_CONTEXT *tcti_ctx,
    size_t size,
    const uint8_t *cmd_buf)
{
    (void) tcti_ctx;
    (void) size;
    (void) cmd_buf;
    ((TSS2_TCTI_FUZZING_CONTEXT*) tcti_ctx)->common.state = TCTI_STATE_RECEIVE;
    return TSS2_RC_SUCCESS;
}

TSS2_RC
tcti_fuzzing_cancel (
    TSS2_TCTI_CONTEXT *tcti_ctx)
{
    (void) tcti_ctx;
    return TSS2_RC_SUCCESS;
}

TSS2_RC
tcti_fuzzing_set_locality (
    TSS2_TCTI_CONTEXT *tcti_ctx,
    uint8_t locality)
{
    (void) tcti_ctx;
    (void) locality;
    return TSS2_RC_SUCCESS;
}

TSS2_RC
tcti_fuzzing_get_poll_handles (
    TSS2_TCTI_CONTEXT *tcti_ctx,
    TSS2_TCTI_POLL_HANDLE *handles,
    size_t *num_handles)
{
    (void)(tcti_ctx);
    (void)(handles);
    (void)(num_handles);
    return TSS2_TCTI_RC_NOT_IMPLEMENTED;
}

void
tcti_fuzzing_finalize(
    TSS2_TCTI_CONTEXT *tcti_ctx)
{
    (void)(tcti_ctx);
}

TSS2_RC
tcti_fuzzing_receive (
    TSS2_TCTI_CONTEXT *tcti_ctx,
    size_t *response_size,
    unsigned char *response_buffer,
    int32_t timeout)
{
    TSS2_TCTI_FUZZING_CONTEXT *tcti_fuzzing = tcti_fuzzing_context_cast (tcti_ctx);
    TSS2_TCTI_COMMON_CONTEXT *tcti_common = tcti_fuzzing_down_cast (tcti_fuzzing);
    TSS2_RC rc;

    rc = tcti_common_receive_checks (tcti_common, response_size);
    if (rc != TSS2_RC_SUCCESS) {
        return rc;
    }
    if (timeout != TSS2_TCTI_TIMEOUT_BLOCK) {
        LOG_WARNING ("The underlying IPC mechanism does not support "
                     "asynchronous I/O. The 'timeout' parameter must be "
                     "TSS2_TCTI_TIMEOUT_BLOCK");
        return TSS2_TCTI_RC_BAD_VALUE;
    }
    if (response_buffer == NULL) {
        LOG_DEBUG ("Caller queried for size but linux kernel doesn't allow this. "
                   "Returning 4k which is the max size for a response buffer.");
        *response_size = tcti_fuzzing->size;
        return TSS2_RC_SUCCESS;
    }
    if (*response_size < tcti_fuzzing->size) {
        LOG_INFO ("Caller provided buffer that *may* not be large enough to "
                  "hold the response buffer.");
    }

    /* Receive the TPM response. */
    *response_size = tcti_fuzzing->size;
    tcti_common->header.size = 0;
    tcti_common->state = TCTI_STATE_TRANSMIT;
    memcpy(response_buffer, tcti_fuzzing->data, *response_size);

    return rc;
}

void
tcti_fuzzing_init_context_data (
    TSS2_TCTI_COMMON_CONTEXT *tcti_common)
{
    TSS2_TCTI_MAGIC (tcti_common) = TCTI_FUZZING_MAGIC;
    TSS2_TCTI_VERSION (tcti_common) = TCTI_VERSION;
    TSS2_TCTI_TRANSMIT (tcti_common) = tcti_fuzzing_transmit;
    TSS2_TCTI_RECEIVE (tcti_common) = tcti_fuzzing_receive;
    TSS2_TCTI_FINALIZE (tcti_common) = tcti_fuzzing_finalize;
    TSS2_TCTI_CANCEL (tcti_common) = tcti_fuzzing_cancel;
    TSS2_TCTI_GET_POLL_HANDLES (tcti_common) = tcti_fuzzing_get_poll_handles;
    TSS2_TCTI_SET_LOCALITY (tcti_common) = tcti_fuzzing_set_locality;
    TSS2_TCTI_MAKE_STICKY (tcti_common) = tcti_make_sticky_not_implemented;
    tcti_common->state = TCTI_STATE_TRANSMIT;
    tcti_common->locality = 3;
    memset (&tcti_common->header, 0, sizeof (tcti_common->header));
}

/*
 * This is an implementation of the standard TCTI initialization function for
 * this module.
 */
TSS2_RC
Tss2_Tcti_Fuzzing_Init (
    TSS2_TCTI_CONTEXT *tcti_ctx,
    size_t *size,
    const char *conf)
{
    (void) conf;

    if (size == NULL) {
        return TSS2_TCTI_RC_BAD_VALUE;
    }
    if (tcti_ctx == NULL) {
        *size = sizeof (TSS2_TCTI_FUZZING_CONTEXT);
        return TSS2_RC_SUCCESS;
    }
    if (*size != sizeof (TSS2_TCTI_FUZZING_CONTEXT)) {
        return TSS2_TCTI_RC_BAD_VALUE;
    }

    tcti_fuzzing_init_context_data (
            &(((TSS2_TCTI_FUZZING_CONTEXT*) tcti_ctx)->common));

    return TSS2_RC_SUCCESS;
}

/* public info structure */
const TSS2_TCTI_INFO tss2_tcti_info = {
    .version = TCTI_VERSION,
    .name = "tcti-fuzzing",
    .description = "TCTI module for fuzzing the System API.",
    .config_help = "Takes no configuration.",
    .init = Tss2_Tcti_Fuzzing_Init,
};

const TSS2_TCTI_INFO*
Tss2_Tcti_Info (void)
{
    return &tss2_tcti_info;
}
