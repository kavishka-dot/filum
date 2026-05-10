#ifndef FILUM_FL_VERSION_H
#define FILUM_FL_VERSION_H

/**
 * @file fl_version.h
 * @brief Filum library version information.
 *
 * Version follows Semantic Versioning 2.0.0 (https://semver.org):
 *   MAJOR: incremented on incompatible API changes
 *   MINOR: incremented on backwards-compatible new functionality
 *   PATCH: incremented on backwards-compatible bug fixes
 *
 * ABI compatibility: within the same MAJOR version, linking against
 * a newer MINOR or PATCH is safe. Changing MAJOR requires recompilation.
 *
 * Wire protocol compatibility: the wire format version is tracked
 * separately via FL_WIRE_VERSION. Shards and Heralds must agree on
 * FL_WIRE_VERSION to interoperate.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Major version number. Incremented on breaking API changes. */
#define FL_VERSION_MAJOR  0

/** @brief Minor version number. Incremented on new backwards-compatible features. */
#define FL_VERSION_MINOR  2

/** @brief Patch version number. Incremented on bug fixes. */
#define FL_VERSION_PATCH  0

/** @brief Full version as a string literal: "MAJOR.MINOR.PATCH". */
#define FL_VERSION_STRING "0.2.0"

/**
 * @brief Wire protocol version.
 *
 * Encoded in the first byte of every FLFrame payload header (reserved field).
 * Shard and Herald reject frames with a mismatched wire version.
 * Increment this when the frame layout or payload structures change.
 */
#define FL_WIRE_VERSION   1

/**
 * @brief Encode version components into a single comparable integer.
 *
 * Example: FL_VERSION_ENCODE(1, 2, 3) == 0x010203
 */
#define FL_VERSION_ENCODE(maj, min, pat) \
    (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | ((uint32_t)(pat)))

/** @brief Current version as a single comparable integer. */
#define FL_VERSION \
    FL_VERSION_ENCODE(FL_VERSION_MAJOR, FL_VERSION_MINOR, FL_VERSION_PATCH)

#include <stdint.h>

/**
 * @brief Runtime version query.
 *
 * Returns the version of the compiled library. Compare against
 * FL_VERSION_MAJOR/MINOR/PATCH (compile-time) to detect mismatches
 * between the headers you compiled against and the library you linked.
 *
 * @param[out] major  Set to FL_VERSION_MAJOR. May be NULL.
 * @param[out] minor  Set to FL_VERSION_MINOR. May be NULL.
 * @param[out] patch  Set to FL_VERSION_PATCH. May be NULL.
 * @return            FL_VERSION as a packed uint32_t.
 */
uint32_t fl_version(uint32_t *major, uint32_t *minor, uint32_t *patch);

/**
 * @brief Return the version string "MAJOR.MINOR.PATCH".
 * @return Pointer to a static string; do not free.
 */
const char *fl_version_string(void);

/**
 * @brief Check that the runtime library is ABI-compatible with the headers.
 *
 * Call this at program startup. Returns 1 if compatible, 0 if not.
 * Incompatible means the MAJOR version of the library differs from
 * the MAJOR version of the headers you compiled against.
 *
 * @code
 * if (!fl_version_compatible()) {
 *     fprintf(stderr, "Filum library version mismatch\n");
 *     exit(1);
 * }
 * @endcode
 */
int fl_version_compatible(void);

#ifdef __cplusplus
}
#endif

#endif /* FILUM_FL_VERSION_H */
