/*
 * cdl - Windows backend (LoadLibrary / GetProcAddress / FreeLibrary).
 * SPDX-License-Identifier: MIT
 */
#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN 1
#endif

#include "cdl_internal.h"

#include <windows.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GetProcAddress returns a function pointer; void* has the same size here. */
typedef char cdl__farproc_size_must_match_void_ptr[
    sizeof(FARPROC) == sizeof(void *) ? 1 : -1];

static wchar_t *cdl__utf8_to_wide(const char *text)
{
    int needed;
    wchar_t *wide;

    if (strlen(text) > (size_t)INT_MAX) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "path is too long for the Windows API");
        return NULL;
    }

    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 text, -1, NULL, 0);
    if (needed <= 0) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "path is not valid UTF-8");
        return NULL;
    }

    if ((size_t)needed > ((size_t)-1) / sizeof(wchar_t)) {
        cdl__set_error(CDL_ERROR_OUT_OF_MEMORY,
                       "path is too long to convert");
        return NULL;
    }

    wide = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (wide == NULL) {
        cdl__set_error(CDL_ERROR_OUT_OF_MEMORY,
                       "out of memory converting path to UTF-16");
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            text, -1, wide, needed) <= 0) {
        cdl__set_error(CDL_ERROR_INVALID_ARGUMENT,
                       "path is not valid UTF-8");
        free(wide);
        return NULL;
    }

    return wide;
}

static char *cdl__wide_to_utf8(const wchar_t *text)
{
    int needed;
    char *utf8;

    needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0)
        return NULL;

    utf8 = (char *)malloc((size_t)needed);
    if (utf8 == NULL)
        return NULL;

    if (WideCharToMultiByte(CP_UTF8, 0, text, -1,
                            utf8, needed, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }

    return utf8;
}

static void cdl__windows_error_text(DWORD error, char *buffer, size_t capacity)
{
    wchar_t *wide_message = NULL;
    DWORD length;
    char *utf8;

    buffer[0] = '\0';

    length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, error,
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            (LPWSTR)&wide_message, 0, NULL);

    if (length > 0 && wide_message != NULL) {
        while (length > 0) {
            wchar_t last = wide_message[length - 1];
            if (last != L'\r' && last != L'\n' &&
                last != L' ' && last != L'\t')
                break;
            wide_message[--length] = L'\0';
        }

        utf8 = cdl__wide_to_utf8(wide_message);
        if (utf8 != NULL) {
            (void)snprintf(buffer, capacity, "%s", utf8);
            free(utf8);
        }
        LocalFree(wide_message);
    }

    if (buffer[0] == '\0')
        (void)snprintf(buffer, capacity, "Windows error %lu",
                       (unsigned long)error);
}

static void cdl__set_windows_error(cdl_error_t code, const char *operation,
                                   DWORD error)
{
    char detail[512];

    cdl__windows_error_text(error, detail, sizeof(detail));
    cdl__set_error(code, "%s failed (Windows error %lu): %s",
                   operation, (unsigned long)error, detail);
}

cdl_handle_t cdl__platform_open(const char *path, unsigned int flags)
{
    wchar_t *wide_path = NULL;
    HMODULE module = NULL;

    /*
     * The Windows loader has no equivalent of these POSIX behaviours.  Failing
     * is more honest than silently changing the caller's requested semantics.
     */
    if ((flags & CDL_DEEPBIND) != 0u) {
        cdl__set_error(CDL_ERROR_NOT_SUPPORTED,
                       "CDL_DEEPBIND is not supported on Windows");
        return NULL;
    }
    if ((flags & CDL_NODELETE) != 0u) {
        cdl__set_error(CDL_ERROR_NOT_SUPPORTED,
                       "CDL_NODELETE is not supported on Windows");
        return NULL;
    }

    if (path != NULL) {
        wide_path = cdl__utf8_to_wide(path);
        if (wide_path == NULL)
            return NULL;
    }

    if ((flags & CDL_NOLOAD) != 0u) {
        /* GetModuleHandleExW increments the reference count, so close balances. */
        if (!GetModuleHandleExW(0, wide_path, &module)) {
            DWORD error = GetLastError();
            cdl__set_windows_error(CDL_ERROR_OPEN_FAILED,
                                   "GetModuleHandleExW", error);
            free(wide_path);
            return NULL;
        }
    } else if (wide_path != NULL) {
        module = LoadLibraryW(wide_path);
        if (module == NULL) {
            DWORD error = GetLastError();
            cdl__set_windows_error(CDL_ERROR_OPEN_FAILED,
                                   "LoadLibraryW", error);
            free(wide_path);
            return NULL;
        }
    } else {
        /*
         * NULL means "the main program".  Use GetModuleHandleExW instead of
         * GetModuleHandleW so that the reference count can be balanced by
         * FreeLibrary() in cdl_close().
         */
        if (!GetModuleHandleExW(0, NULL, &module)) {
            DWORD error = GetLastError();
            cdl__set_windows_error(CDL_ERROR_OPEN_FAILED,
                                   "GetModuleHandleExW", error);
            return NULL;
        }
    }

    free(wide_path);
    return (cdl_handle_t)module;
}

void *cdl__platform_sym(cdl_handle_t handle, const char *name)
{
    FARPROC procedure = NULL;
    void *symbol = NULL;

    procedure = GetProcAddress((HMODULE)handle, name);
    if (procedure == NULL) {
        cdl__set_error(CDL_ERROR_SYMBOL_NOT_FOUND,
                       "GetProcAddress(\"%s\") failed: "
                       "the module does not export this symbol", name);
        return NULL;
    }

    memcpy(&symbol, &procedure, sizeof(symbol));
    return symbol;
}

int cdl__platform_close(cdl_handle_t handle)
{
    if (!FreeLibrary((HMODULE)handle)) {
        DWORD error = GetLastError();
        cdl__set_windows_error(CDL_ERROR_CLOSE_FAILED,
                               "FreeLibrary", error);
        return -1;
    }

    return 0;
}
