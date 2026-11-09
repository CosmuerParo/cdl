/*
 * cdl - generic API, error handling and argument validation.
 * SPDX-License-Identifier: MIT
 */
#include "cdl_internal.h"

#include <stdio.h>

CDL_TLS cdl_error_state cdl__error_state = { CDL_ERROR_NONE, { 0 } };

void cdl__clear_error(void)
{
    cdl__error_state.code = CDL_ERROR_NONE;
    cdl__error_state.message[0] = '\0';
}

void cdl__set_error_v(cdl_error_t code, const char *format, va_list args)
{
    int written;

    cdl__error_state.code = code;
    cdl__error_state.message[0] = '\0';

    if (format == NULL || format[0] == '\0')
        format = "unknown error";

    written = vsnprintf(cdl__error_state.message,
                        sizeof(cdl__error_state.message),
                        format, args);
    (void)written;

    /* Be defensive about old vsnprintf implementations and truncation. */
    cdl__error_state.message[sizeof(cdl__error_state.message) - 1] = '\0';
    if (cdl__error_state.message[0] == '\0')
        (void)snprintf(cdl__error_state.message,
                       sizeof(cdl__error_state.message),
                       "cdl error %d", (int)code);
}

void cdl__set_error(cdl_error_t code, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    cdl__set_error_v(code, format, args);
    va_end(args);
}

const char *cdl_error(void)
{
    if (cdl__error_state.code == CDL_ERROR_NONE)
        return NULL;
    if (cdl__error_state.message[0] == '\0')
        return "cdl: unknown error";
    return cdl__error_state.message;
}

cdl_error_t cdl_last_error(void)
{
    return cdl__error_state.code;
}

void cdl_error_clear(void)
{
    cdl__clear_error();
}

const char *cdl_version(void)
{
    return CDL_VERSION_STRING;
}

#define CDL_OPEN_FLAGS_KNOWN \
    ((unsigned int)(CDL_LAZY | CDL_NOW | CDL_GLOBAL | CDL_LOCAL | \
                    CDL_DEEPBIND | CDL_NODELETE | CDL_NOLOAD))

static int cdl__flags_valid(unsigned int flags)
{
    if ((flags & ~CDL_OPEN_FLAGS_KNOWN) != 0u)
        return 0;
    if ((flags & CDL_LAZY) != 0u && (flags & CDL_NOW) != 0u)
        return 0;
    if ((flags & CDL_GLOBAL) != 0u && (flags & CDL_LOCAL) != 0u)
        return 0;
    return 1;
}

cdl_handle_t cdl_open(const char *path, unsigned int flags)
{
    cdl__clear_error();

    if (!cdl__flags_valid(flags)) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "invalid cdl_open flags: 0x%x "
                       "(LAZY/NOW and GLOBAL/LOCAL are mutually exclusive)",
                       flags);
        return NULL;
    }

    return cdl__platform_open(path, flags);
}

void *cdl_sym(cdl_handle_t handle, const char *name)
{
    cdl__clear_error();

    if (handle == NULL) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "cdl_sym: handle is NULL");
        return NULL;
    }
    if (name == NULL || name[0] == '\0') {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "cdl_sym: symbol name is NULL or empty");
        return NULL;
    }

    return cdl__platform_sym(handle, name);
}

int cdl_close(cdl_handle_t handle)
{
    cdl__clear_error();

    if (handle == NULL) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "cdl_close: handle is NULL");
        return -1;
    }

    return cdl__platform_close(handle);
}
