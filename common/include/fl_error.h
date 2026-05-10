#ifndef FILUM_FL_ERROR_H
#define FILUM_FL_ERROR_H

/**
 * @file fl_error.h
 * @brief Filum error codes.
 *
 * Every public Filum function returns @ref FLError.
 * The value @ref FL_OK (zero) means success; all other values are failures.
 * Negative values are unrecoverable errors. Positive values are soft conditions
 * (e.g. timeout, end-of-data) that the caller may handle and continue.
 *
 * @code
 * FLError err = fl_shard_init(&shard, &hal, id, &model);
 * if (err != FL_OK) {
 *     // handle error
 * }
 * @endcode
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return type for all public Filum API functions.
 *
 * Zero is success. Negative values are errors. Positive values are
 * informational conditions (timeout, not-ready) that are not failures.
 */
typedef enum {

    /* -----------------------------------------------------------------------
     * Success
     * --------------------------------------------------------------------- */

    /** Operation completed successfully. */
    FL_OK                   =  0,

    /* -----------------------------------------------------------------------
     * Soft conditions (positive) — not errors, caller decides what to do
     * --------------------------------------------------------------------- */

    /** No data available right now; try again later. */
    FL_AGAIN                =  1,

    /** Timed out waiting for a response. */
    FL_TIMEOUT              =  2,

    /** Reassembly in progress; more fragments expected. */
    FL_PENDING              =  3,

    /* -----------------------------------------------------------------------
     * Errors (negative) — operation failed
     * --------------------------------------------------------------------- */

    /** Unspecified internal error. */
    FL_ERR                  = -1,

    /** A required argument was NULL or invalid. */
    FL_ERR_INVALID_ARG      = -2,

    /** Output buffer is too small for the result. */
    FL_ERR_BUFFER_TOO_SMALL = -3,

    /** Requested operation exceeds compile-time capacity limit. */
    FL_ERR_CAPACITY         = -4,

    /** CRC check failed; frame is corrupt. */
    FL_ERR_CRC              = -5,

    /** Frame magic bytes are wrong; not a Filum frame. */
    FL_ERR_BAD_MAGIC        = -6,

    /** Cryptographic authentication (Poly1305 MAC) failed. */
    FL_ERR_AUTH             = -7,

    /** ECDH handshake has not completed; session key not available. */
    FL_ERR_NO_SESSION       = -8,

    /** Hardware abstraction layer (HAL) returned an error. */
    FL_ERR_HAL              = -9,

    /** Non-volatile storage read or write failed. */
    FL_ERR_NVS              = -10,

    /** Fragment pool is full; cannot accept more concurrent shards. */
    FL_ERR_POOL_FULL        = -11,

    /** Duplicate fragment received; already seen this frag_index. */
    FL_ERR_DUPLICATE        = -12,

    /** Serial port or transport could not be opened. */
    FL_ERR_TRANSPORT        = -13,

    /** Operation not permitted in the current state. */
    FL_ERR_STATE            = -14,

    /** Feature is disabled (e.g. DP disabled, crypto disabled). */
    FL_ERR_DISABLED         = -15,

} FLError;

/**
 * @brief Return a human-readable string for an error code.
 *
 * Always returns a valid non-NULL string. Unknown codes return
 * @c "FL_ERR_UNKNOWN".
 *
 * @param err  Error code to describe.
 * @return     Pointer to a static string; do not free.
 */
const char *fl_strerror(FLError err);

/**
 * @brief Return 1 if @p err represents a successful outcome, 0 otherwise.
 *
 * Both @ref FL_OK and positive soft conditions (FL_AGAIN, FL_TIMEOUT, etc.)
 * are considered "not an error" for the purposes of logging and assertion.
 * Use @c err == FL_OK when you need strict success.
 */
static inline int fl_ok(FLError err) { return err >= 0; }

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_ERROR_H */
