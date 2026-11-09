/*
 * cdl test suite.
 * SPDX-License-Identifier: MIT
 */
#include <cdl/cdl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shellapi.h>
#endif

#if defined(_WIN32)
#  define CDL_TEST_PLATFORM "windows"
#  define CDL_TEST_MISSING_MODULE "cdl_missing_module_98765.dll"
#elif defined(__ANDROID__)
#  define CDL_TEST_PLATFORM "android"
#  define CDL_TEST_MISSING_MODULE "libcdl_missing_module_98765.so"
#else
#  define CDL_TEST_PLATFORM "linux"
#  define CDL_TEST_MISSING_MODULE "libcdl_missing_module_98765.so"
#endif

typedef int (*cdl_add_fn)(int, int);

/* Function and object pointers have the same representation on all targets. */
typedef char cdl_add_fn_size_check[
    sizeof(cdl_add_fn) == sizeof(void *) ? 1 : -1];

static int failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n",                              \
                    __FILE__, __LINE__, #condition);                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static const char *error_text(void)
{
    const char *text = cdl_error();
    return text != NULL ? text : "(no error)";
}

#if defined(_WIN32)
/*
 * CTest passes a Unicode path, but main()'s argv is in the ANSI code page.
 * Read the wide command line so the test can hand cdl a real UTF-8 path.
 */
static char *utf8_module_path(void)
{
    int wide_argc = 0;
    LPWSTR *wide_argv;
    char *path = NULL;

    wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == NULL)
        return NULL;

    if (wide_argc == 2) {
        int needed = WideCharToMultiByte(CP_UTF8, 0, wide_argv[1], -1,
                                         NULL, 0, NULL, NULL);
        if (needed > 0) {
            path = (char *)malloc((size_t)needed);
            if (path != NULL &&
                WideCharToMultiByte(CP_UTF8, 0, wide_argv[1], -1,
                                    path, needed, NULL, NULL) <= 0) {
                free(path);
                path = NULL;
            }
        }
    }

    LocalFree(wide_argv);
    return path;
}
#endif

static void check_last_error(cdl_error_t expected, int line)
{
    if (cdl_last_error() != expected) {
        fprintf(stderr,
                "FAIL %s:%d: expected error %d, got %d (%s)\n",
                __FILE__, line, (int)expected,
                (int)cdl_last_error(), error_text());
        failures++;
    }
}

#define CHECK_ERROR(expected) check_last_error((expected), __LINE__)

static int symbol_add(cdl_handle_t handle)
{
    void *symbol;
    cdl_add_fn add = NULL;

    symbol = cdl_sym(handle, "cdl_test_add");
    CHECK(symbol != NULL);
    if (symbol == NULL)
        return 0;

    memcpy(&add, &symbol, sizeof(add));
    return add(20, 22);
}

int main(int argc, char **argv)
{
    const char *module_path;
    char *module_path_utf8 = NULL;
    cdl_handle_t handle;
    cdl_handle_t no_load;
    void *symbol;
    int value;

    printf("cdl %s, platform: %s\n", cdl_version(), CDL_TEST_PLATFORM);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <path-to-cdl_test_module>\n", argv[0]);
        return 2;
    }

    module_path = argv[1];
#if defined(_WIN32)
    module_path_utf8 = utf8_module_path();
    if (module_path_utf8 != NULL)
        module_path = module_path_utf8;
#endif

    CHECK(strcmp(cdl_version(), CDL_VERSION_STRING) == 0);

    CHECK(cdl_open(module_path, CDL_LAZY | CDL_NOW) == NULL);
    CHECK_ERROR(CDL_ERROR_INVALID_ARGUMENT);

    CHECK(cdl_open(module_path, CDL_GLOBAL | CDL_LOCAL) == NULL);
    CHECK_ERROR(CDL_ERROR_INVALID_ARGUMENT);

    CHECK(cdl_sym(NULL, "anything") == NULL);
    CHECK_ERROR(CDL_ERROR_INVALID_ARGUMENT);

    CHECK(cdl_close(NULL) == -1);
    CHECK_ERROR(CDL_ERROR_INVALID_ARGUMENT);

#if defined(_WIN32)
    CHECK(cdl_open("\xFF", 0) == NULL);
    CHECK_ERROR(CDL_ERROR_INVALID_ARGUMENT);
#endif

    handle = cdl_open(CDL_TEST_MISSING_MODULE, 0);
    CHECK(handle == NULL);
    CHECK_ERROR(CDL_ERROR_OPEN_FAILED);
    CHECK(cdl_error() != NULL && cdl_error()[0] != '\0');
    if (handle != NULL)
        (void)cdl_close(handle);

    /* Main-program handle, equivalent to dlopen(NULL, ...). */
    handle = cdl_open(NULL, 0);
    CHECK(handle != NULL);
    if (handle != NULL) {
        CHECK(cdl_close(handle) == 0);
    }

    /* Normal load, function symbol, data symbol, missing symbol. */
    handle = cdl_open(module_path, CDL_NOW | CDL_LOCAL);
    CHECK(handle != NULL);
    if (handle == NULL) {
        fprintf(stderr, "cdl_open(%s) failed: %s\n",
                module_path, error_text());
        free(module_path_utf8);
        return 1;
    }

    CHECK(symbol_add(handle) == 42);
    CHECK_ERROR(CDL_ERROR_NONE);
    CHECK(cdl_error() == NULL);

    symbol = cdl_sym(handle, "cdl_test_value");
    CHECK(symbol != NULL);
    value = 0;
    if (symbol != NULL)
        memcpy(&value, symbol, sizeof(value));
    CHECK(value == 42);

    CHECK(cdl_sym(handle, "cdl_test_does_not_exist") == NULL);
    CHECK_ERROR(CDL_ERROR_SYMBOL_NOT_FOUND);
    CHECK(cdl_error() != NULL && cdl_error()[0] != '\0');

    /* RTLD_NOLOAD: the module is already loaded at this point. */
    no_load = cdl_open(module_path, CDL_NOW | CDL_NOLOAD);
    CHECK(no_load != NULL);
    if (no_load != NULL) {
        CHECK(symbol_add(no_load) == 42);
        CHECK(cdl_close(no_load) == 0);
    }

    CHECK(cdl_close(handle) == 0);
    CHECK_ERROR(CDL_ERROR_NONE);
    CHECK(cdl_error() == NULL);

#if defined(_WIN32)
    handle = cdl_open(module_path, CDL_DEEPBIND);
    CHECK(handle == NULL);
    CHECK_ERROR(CDL_ERROR_NOT_SUPPORTED);

    handle = cdl_open(module_path, CDL_NODELETE);
    CHECK(handle == NULL);
    CHECK_ERROR(CDL_ERROR_NOT_SUPPORTED);
#elif defined(__linux__) && !defined(__ANDROID__)
    handle = cdl_open(module_path, CDL_DEEPBIND | CDL_NODELETE);
    CHECK(handle != NULL);
    if (handle != NULL) {
        CHECK(symbol_add(handle) == 42);
        CHECK(cdl_close(handle) == 0);
    }
#endif

    cdl_error_clear();
    CHECK_ERROR(CDL_ERROR_NONE);
    CHECK(cdl_error() == NULL);

    free(module_path_utf8);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all tests passed\n");
    return 0;
}
