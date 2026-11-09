/*
 * Internal declarations shared by the generic and platform-specific sources.
 * SPDX-License-Identifier: MIT
 */
#ifndef CDL_INTERNAL_H
#define CDL_INTERNAL_H

#include "cdl/cdl.h"

#include <stdarg.h>
#include <stddef.h>

#if defined(_MSC_VER)
#  define CDL_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define CDL_TLS __thread
#else
#  error "cdl needs compiler support for thread-local storage"
#endif

#define CDL_ERROR_MESSAGE_SIZE 1024

typedef struct cdl_error_state {
    cdl_error_t code;
    char message[CDL_ERROR_MESSAGE_SIZE];
} cdl_error_state;

/* One error state per thread; all public entry points operate on this. */
extern CDL_TLS cdl_error_state cdl__error_state;

/* Set the current thread's error state.  format must not be NULL. */
void cdl__set_error(cdl_error_t code, const char *format, ...);
void cdl__set_error_v(cdl_error_t code, const char *format, va_list args);

/* Clear the current thread's error state. */
void cdl__clear_error(void);

/*
 * Platform backends.  Open receives already validated flags, but still has to
 * reject flags that the platform cannot implement.
 */
cdl_handle_t cdl__platform_open(const char *path, unsigned int flags);
void *cdl__platform_sym(cdl_handle_t handle, const char *name);
int cdl__platform_close(cdl_handle_t handle);

#endif /* CDL_INTERNAL_H */
