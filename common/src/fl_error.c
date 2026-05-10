/*
 * filum/common/src/fl_error.c
 */

#include "fl_error.h"

const char *fl_strerror(FLError err)
{
    switch (err) {
        case FL_OK:                   return "FL_OK";
        case FL_AGAIN:                return "FL_AGAIN";
        case FL_TIMEOUT:              return "FL_TIMEOUT";
        case FL_PENDING:              return "FL_PENDING";
        case FL_ERR:                  return "FL_ERR";
        case FL_ERR_INVALID_ARG:      return "FL_ERR_INVALID_ARG";
        case FL_ERR_BUFFER_TOO_SMALL: return "FL_ERR_BUFFER_TOO_SMALL";
        case FL_ERR_CAPACITY:         return "FL_ERR_CAPACITY";
        case FL_ERR_CRC:              return "FL_ERR_CRC";
        case FL_ERR_BAD_MAGIC:        return "FL_ERR_BAD_MAGIC";
        case FL_ERR_AUTH:             return "FL_ERR_AUTH";
        case FL_ERR_NO_SESSION:       return "FL_ERR_NO_SESSION";
        case FL_ERR_HAL:              return "FL_ERR_HAL";
        case FL_ERR_NVS:              return "FL_ERR_NVS";
        case FL_ERR_POOL_FULL:        return "FL_ERR_POOL_FULL";
        case FL_ERR_DUPLICATE:        return "FL_ERR_DUPLICATE";
        case FL_ERR_TRANSPORT:        return "FL_ERR_TRANSPORT";
        case FL_ERR_STATE:            return "FL_ERR_STATE";
        case FL_ERR_DISABLED:         return "FL_ERR_DISABLED";
        default:                      return "FL_ERR_UNKNOWN";
    }
}
