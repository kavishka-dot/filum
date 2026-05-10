/*
 * filum/common/src/fl_version.c
 */

#include "fl_version.h"

uint32_t fl_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    if (major) *major = FL_VERSION_MAJOR;
    if (minor) *minor = FL_VERSION_MINOR;
    if (patch) *patch = FL_VERSION_PATCH;
    return FL_VERSION;
}

const char *fl_version_string(void)
{
    return FL_VERSION_STRING;
}

int fl_version_compatible(void)
{
    /* ABI compatible if MAJOR matches */
    return FL_VERSION_MAJOR == FL_VERSION_MAJOR;  /* always true for same header */
}
