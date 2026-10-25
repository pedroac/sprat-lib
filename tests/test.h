/* Tiny test harness. Every check records a failure and keeps going, and says
 * what it saw: CHECK_INT(sprite->rect.w, 16) prints
 *   test_sheet.c:42: sprite->rect.w is 15, expected 16 */
#ifndef SPRAT_TEST_H
#define SPRAT_TEST_H

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define FAIL_AT(...)                                     \
    do {                                                 \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__);  \
        fprintf(stderr, __VA_ARGS__);                    \
        fprintf(stderr, "\n");                           \
        ++g_failures;                                    \
    } while (0)

#define CHECK(cond)                                    \
    do {                                               \
        if (!(cond)) FAIL_AT("CHECK failed: %s", #cond); \
    } while (0)

#define CHECK_INT(actual, expected)                                                       \
    do {                                                                                  \
        long long actual_ = (long long)(actual);                                          \
        long long expected_ = (long long)(expected);                                      \
        if (actual_ != expected_) {                                                       \
            FAIL_AT("%s is %lld, expected %lld", #actual, actual_, expected_);            \
        }                                                                                 \
    } while (0)

#define CHECK_SIZE(actual, expected)                                                      \
    do {                                                                                  \
        unsigned long long actual_ = (unsigned long long)(actual);                        \
        unsigned long long expected_ = (unsigned long long)(expected);                    \
        if (actual_ != expected_) {                                                       \
            FAIL_AT("%s is %llu, expected %llu", #actual, actual_, expected_);            \
        }                                                                                 \
    } while (0)

#define CHECK_DOUBLE(actual, expected)                                                    \
    do {                                                                                  \
        double actual_ = (actual);                                                        \
        double expected_ = (expected);                                                    \
        if (actual_ != expected_) {                                                       \
            FAIL_AT("%s is %.17g, expected %.17g", #actual, actual_, expected_);          \
        }                                                                                 \
    } while (0)

/* NULL on either side is compared and printed as NULL. */
#define CHECK_STR(actual, expected)                                                       \
    do {                                                                                  \
        const char* actual_ = (actual);                                                   \
        const char* expected_ = (expected);                                               \
        if (actual_ == NULL || expected_ == NULL ? actual_ != expected_                   \
                                                 : strcmp(actual_, expected_) != 0) {     \
            FAIL_AT("%s is %s%s%s, expected %s%s%s", #actual, actual_ ? "\"" : "",        \
                    actual_ ? actual_ : "NULL", actual_ ? "\"" : "", expected_ ? "\"" : "", \
                    expected_ ? expected_ : "NULL", expected_ ? "\"" : "");               \
        }                                                                                 \
    } while (0)

#define CHECK_NULL(actual)                                                                \
    do {                                                                                  \
        if ((actual) != NULL) FAIL_AT("%s is not NULL", #actual);                         \
    } while (0)

#define CHECK_NOT_NULL(actual)                                                            \
    do {                                                                                  \
        if ((actual) == NULL) FAIL_AT("%s is NULL", #actual);                             \
    } while (0)

#define RUN(test)                                                 \
    do {                                                          \
        int before_ = g_failures;                                 \
        test();                                                   \
        fprintf(stderr, "%s %s\n",                                \
                g_failures == before_ ? "ok  " : "FAIL", #test);  \
    } while (0)

#define TEST_RESULT() (g_failures == 0 ? 0 : 1)

#endif /* SPRAT_TEST_H */
