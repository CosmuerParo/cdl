/*
 * cdl - a small, cross-platform C wrapper around libdl / the Windows loader.
 *
 * Supported platforms: Windows, Linux, Android.
 * SPDX-License-Identifier: MIT
 */
#ifndef CDL_CDL_H
#define CDL_CDL_H

#include <stddef.h>

#if defined(_WIN32)
#  define CDL_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#  define CDL_PLATFORM_ANDROID 1
#elif defined(__linux__)
#  define CDL_PLATFORM_LINUX 1
#else
#  error "cdl supports Windows, Linux and Android only"
#endif

/*
 * CDL_API controls symbol visibility.
 *
 * CMake defines CDL_BUILDING while building the shared library and CDL_STATIC
 * for consumers of the static library.  Users who compile the sources by hand
 * must define CDL_BUILDING when creating a Windows DLL, and CDL_STATIC when
 * linking the Windows static library.
 */
#if defined(_WIN32)
#  if defined(CDL_STATIC)
#    define CDL_API
#  elif defined(CDL_BUILDING)
#    define CDL_API __declspec(dllexport)
#  else
#    define CDL_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define CDL_API __attribute__((visibility("default")))
#else
#  define CDL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CDL_VERSION_MAJOR 1
#define CDL_VERSION_MINOR 0
#define CDL_VERSION_PATCH 0
#define CDL_VERSION_STRING "1.0.0"

/*
 * A loaded module.  NULL is never a valid handle.
 *
 * On POSIX systems this is the void* returned by dlopen(); on Windows it is an
 * HMODULE.  Call cdl_close() exactly once for every successful cdl_open().
 */
typedef void *cdl_handle_t;

/*
 * Flags accepted by cdl_open().
 *
 * CDL_LAZY / CDL_NOW and CDL_GLOBAL / CDL_LOCAL are mutually exclusive pairs.
 * If neither member of a pair is given, CDL_NOW and CDL_LOCAL are used.
 *
 * Windows notes:
 *   - Lazy binding is not a Windows concept, so CDL_LAZY and CDL_NOW are
 *     accepted and have the same effect.
 *   - Windows makes exports process-wide, so CDL_GLOBAL and CDL_LOCAL are
 *     accepted and have no effect.
 *   - CDL_NOLOAD is implemented with GetModuleHandleExW().
 *   - CDL_DEEPBIND and CDL_NODELETE are not supported and make cdl_open()
 *     fail with CDL_ERROR_NOT_SUPPORTED.
 *
 * Android notes:
 *   - CDL_DEEPBIND is not available in bionic and is rejected.
 *   - CDL_NODELETE and CDL_NOLOAD are available in the NDK headers.
 */
typedef enum cdl_open_flag {
    CDL_LAZY     = 1u << 0,
    CDL_NOW      = 1u << 1,
    CDL_GLOBAL   = 1u << 2,
    CDL_LOCAL    = 1u << 3,
    CDL_DEEPBIND = 1u << 4,
    CDL_NODELETE = 1u << 5,
    CDL_NOLOAD   = 1u << 6
} cdl_open_flag_t;

/* Last error recorded on the calling thread. */
typedef enum cdl_error_code {
    CDL_ERROR_NONE = 0,
    CDL_ERROR_INVALID_ARGUMENT,
    CDL_ERROR_OUT_OF_MEMORY,
    CDL_ERROR_NOT_SUPPORTED,
    CDL_ERROR_OPEN_FAILED,
    CDL_ERROR_SYMBOL_NOT_FOUND,
    CDL_ERROR_CLOSE_FAILED
} cdl_error_t;

/*
 * Open a module.
 *
 * path is a UTF-8 path on Windows and a native byte string on Linux/Android.
 * Passing NULL opens the main program (dlopen(NULL) semantics); on Windows
 * this returns a reference to the executable module.
 *
 * flags is a bitwise OR of cdl_open_flag_t values; 0 means CDL_NOW | CDL_LOCAL.
 *
 * Returns NULL on failure.  Use cdl_error() / cdl_last_error() for details.
 */
CDL_API cdl_handle_t cdl_open(const char *path, unsigned int flags);

/*
 * Look up an exported symbol.
 *
 * Returns the symbol address, or NULL on failure.  A NULL return with
 * cdl_last_error() == CDL_ERROR_NONE means the symbol exists but its value is
 * NULL (possible with POSIX data symbols); on Windows NULL is always an error.
 *
 * The returned void* may be converted to an object pointer directly.  C does
 * not guarantee a conversion between void* and function pointers, so code that
 * calls a function symbol should use memcpy() (or a union) into the desired
 * function pointer type.  cdl itself uses memcpy() on Windows for this reason.
 */
CDL_API void *cdl_sym(cdl_handle_t handle, const char *name);

/*
 * Release a handle obtained from cdl_open().
 *
 * Returns 0 on success and -1 on failure.  Do not use the handle after a
 * successful close.
 */
CDL_API int cdl_close(cdl_handle_t handle);

/*
 * Return the current thread's last error message, or NULL if no error is
 * pending.  The pointer is owned by cdl and remains valid until the next
 * error-changing call (cdl_open, cdl_sym, cdl_close, or cdl_error_clear) on
 * the same thread; do not free it.
 */
CDL_API const char *cdl_error(void);

/* Return the current thread's last error code. */
CDL_API cdl_error_t cdl_last_error(void);

/* Clear the current thread's error state. */
CDL_API void cdl_error_clear(void);

/* Return the library version, for example "1.0.0". */
CDL_API const char *cdl_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CDL_CDL_H */
