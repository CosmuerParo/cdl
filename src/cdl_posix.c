/*
 * cdl - Linux and Android backend (POSIX dlopen API).
 * SPDX-License-Identifier: MIT
 */
#if !defined(_GNU_SOURCE)
#  define _GNU_SOURCE 1
#endif

#include "cdl_internal.h"

#include <dlfcn.h>

static int cdl__posix_flags(unsigned int flags)
{
    int result;

    result = (flags & CDL_LAZY) != 0u ? RTLD_LAZY : RTLD_NOW;
    result |= (flags & CDL_GLOBAL) != 0u ? RTLD_GLOBAL : RTLD_LOCAL;

    if ((flags & CDL_DEEPBIND) != 0u) {
#if defined(RTLD_DEEPBIND)
        result |= RTLD_DEEPBIND;
#else
        cdl__set_error(CDL_ERROR_NOT_SUPPORTED,
                       "CDL_DEEPBIND is not supported by this platform");
        return -1;
#endif
    }

    if ((flags & CDL_NODELETE) != 0u) {
#if defined(RTLD_NODELETE)
        result |= RTLD_NODELETE;
#else
        cdl__set_error(CDL_ERROR_NOT_SUPPORTED,
                       "CDL_NODELETE is not supported by this platform");
        return -1;
#endif
    }

    if ((flags & CDL_NOLOAD) != 0u) {
#if defined(RTLD_NOLOAD)
        result |= RTLD_NOLOAD;
#else
        cdl__set_error(CDL_ERROR_NOT_SUPPORTED,
                       "CDL_NOLOAD is not supported by this platform");
        return -1;
#endif
    }

    return result;
}

static const char *cdl__dl_detail(void)
{
    const char *detail = dlerror();
    return detail != NULL ? detail : "unknown dynamic loader error";
}

cdl_handle_t cdl__platform_open(const char *path, unsigned int flags)
{
    int dl_flags;
    void *handle;

    dl_flags = cdl__posix_flags(flags);
    if (dl_flags < 0)
        return NULL;

    /* dlerror() is a thread-local one-shot flag: clear stale state first. */
    (void)dlerror();

    handle = dlopen(path, dl_flags);
    if (handle == NULL) {
        const char *detail = cdl__dl_detail();
        cdl__set_error(CDL_ERROR_OPEN_FAILED,
                       "dlopen(\"%s\") failed: %s",
                       path != NULL ? path : "(self)", detail);
        return NULL;
    }

    return handle;
}

void *cdl__platform_sym(cdl_handle_t handle, const char *name)
{
    void *symbol;

    (void)dlerror();

    symbol = dlsym(handle, name);
    if (symbol == NULL) {
        const char *detail = dlerror();
        if (detail != NULL) {
            cdl__set_error(CDL_ERROR_SYMBOL_NOT_FOUND,
                           "dlsym(\"%s\") failed: %s", name, detail);
        }
        /*
         * If dlerror() is NULL the symbol exists with a NULL value.  This is
         * legal for data symbols; leave CDL_ERROR_NONE intact.
         */
    }

    return symbol;
}

int cdl__platform_close(cdl_handle_t handle)
{
    (void)dlerror();

    if (dlclose(handle) != 0) {
        const char *detail = cdl__dl_detail();
        cdl__set_error(CDL_ERROR_CLOSE_FAILED,
                       "dlclose() failed: %s", detail);
        return -1;
    }

    return 0;
}
