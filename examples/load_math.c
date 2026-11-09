/*
 * Minimal cdl example: load the platform math library and call cos().
 * SPDX-License-Identifier: MIT
 */
#include <cdl/cdl.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef double (*cos_fn)(double);
typedef char cos_fn_size_check[
    sizeof(cos_fn) == sizeof(void *) ? 1 : -1];

static cdl_handle_t open_math(const char *explicit_path)
{
#if defined(_WIN32)
    static const char *const candidates[] = { "msvcrt.dll", NULL };
#elif defined(__ANDROID__)
    static const char *const candidates[] = { "libm.so", NULL };
#else
    static const char *const candidates[] = { "libm.so.6", "libm.so", NULL };
#endif
    int i;

    if (explicit_path != NULL)
        return cdl_open(explicit_path, CDL_NOW | CDL_LOCAL);

    for (i = 0; candidates[i] != NULL; ++i) {
        cdl_handle_t handle = cdl_open(candidates[i], CDL_NOW | CDL_LOCAL);
        if (handle != NULL)
            return handle;
    }

    return NULL;
}

int main(int argc, char **argv)
{
    cdl_handle_t handle;
    void *symbol;
    cos_fn cosine = NULL;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [path-to-math-library]\n", argv[0]);
        return 2;
    }

    handle = open_math(argc == 2 ? argv[1] : NULL);
    if (handle == NULL) {
        fprintf(stderr, "could not load the math library: %s\n",
                cdl_error() != NULL ? cdl_error() : "unknown error");
        return 1;
    }

    symbol = cdl_sym(handle, "cos");
    if (symbol == NULL) {
        fprintf(stderr, "could not find cos: %s\n",
                cdl_error() != NULL ? cdl_error() : "unknown error");
        (void)cdl_close(handle);
        return 1;
    }

    memcpy(&cosine, &symbol, sizeof(cosine));
    printf("cos(0) = %.6f\n", cosine(0.0));

    if (cdl_close(handle) != 0) {
        fprintf(stderr, "could not close the math library: %s\n",
                cdl_error() != NULL ? cdl_error() : "unknown error");
        return 1;
    }

    return 0;
}
