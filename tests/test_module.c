/*
 * Test module loaded by test_cdl.c.
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32)
#  define CDL_TEST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define CDL_TEST_EXPORT __attribute__((visibility("default")))
#else
#  define CDL_TEST_EXPORT
#endif

CDL_TEST_EXPORT int cdl_test_add(int a, int b);
CDL_TEST_EXPORT const char *cdl_test_name(void);

CDL_TEST_EXPORT int cdl_test_add(int a, int b)
{
    return a + b;
}

CDL_TEST_EXPORT int cdl_test_value = 42;

CDL_TEST_EXPORT const char *cdl_test_name(void)
{
    return "cdl test module";
}
