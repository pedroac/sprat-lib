/* The tokenizer: numbers, counts, strings, tags, the end of input, positions
 * and quoting, with exact error messages. */
#include "reader.h"
#include "test.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>

/* Strings are copied into this pool; each reader starts with all of it. */
static char g_pool[256];

static sprat_reader make_reader(const char* text, sprat_error* error) {
    memset(error, 0, sizeof(*error));
    sprat_reader r = {text, strlen(text), 0, 0, error, g_pool, sizeof(g_pool)};
    return r;
}

/* The reader failed with this message at this place. */
#define CHECK_FAILED(r, error, ok, status_, line_, column_, message_)          \
    do {                                                                    \
        CHECK(!(ok));                                                       \
        CHECK_INT((error).status, (status_));                               \
        CHECK_SIZE((error).line, (line_));                                  \
        CHECK_SIZE((error).column, (column_));                              \
        CHECK_STR((error).message, (message_));                             \
    } while (0)

static void test_ints(void) {
    sprat_error error;
    sprat_reader r = make_reader("0 -7 2147483647 -2147483648 -0 007\t5\r\n6", &error);
    int i = 1;
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 0);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, -7);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, INT_MAX);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, INT_MIN);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 0);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 7);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 5);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 6);
    CHECK(sprat_reader_end(&r));
    /* Leading zeros do not count toward the digit limit. */
    r = make_reader("00000000000000000000000000042", &error);
    CHECK(sprat_reader_int(&r, &i));
    CHECK_INT(i, 42);
}

static void test_bad_ints(void) {
    static const struct {
        const char* text;
        const char* message;
    } k_cases[] = {
        {"2147483648", "integer out of range (-2147483648 to 2147483647), found \"2147483648\""},
        {"-2147483649", "integer out of range (-2147483648 to 2147483647), found \"-2147483649\""},
        {"99999999999999999999999", "integer out of range (-2147483648 to 2147483647), found "
                                    "\"99999999999999999999999\""},
        {"1.5", "expected an integer, found \"1.5\""},
        {"+1", "expected an integer, found \"+1\""},
        {"-", "expected an integer, found \"-\""},
        {"--1", "expected an integer, found \"--1\""},
        {"1a", "expected an integer, found \"1a\""},
        {"1e3", "expected an integer, found \"1e3\""},
        {"0x10", "expected an integer, found \"0x10\""},
        {"", "expected an integer, found the end of the file"},
        {"   ", "expected an integer, found the end of the file"},
    };
    for (size_t k = 0; k < sizeof(k_cases) / sizeof(k_cases[0]); ++k) {
        sprat_error error;
        sprat_reader r = make_reader(k_cases[k].text, &error);
        int i = 12345;
        size_t column = strlen(k_cases[k].text) - strlen(k_cases[k].text + strspn(k_cases[k].text, " ")) + 1;
        CHECK_FAILED(r, error, sprat_reader_int(&r, &i), SPRAT_ERR_SYNTAX, 1, column,
                     k_cases[k].message);
        CHECK_INT(i, 12345); /* untouched on failure */
    }
}

static void test_doubles(void) {
    static const struct {
        const char* text;
        double value;
    } k_cases[] = {
        {"0", 0.0},
        {"3", 3.0},
        {"0.5", 0.5},
        {"-0.5", -0.5},
        {"1e-05", 1e-05},
        {"2.5E+3", 2500.0},
        {"2.5e3", 2500.0},
        {"0.10000000000000001", 0.1},
        /* sprat-cli's scales: 8 significant digits printed with %.17g. */
        {"0.33333332999999998", 0.33333333},
        {"0.66666667000000002", 0.66666667},
        {"0.69999999999999996", 0.7},
        {"000.250", 0.25},
        {"0.0000001", 1e-7},
        {"123456789012345678", 123456789012345678.0},
        {"1e22", 1e22},
        {"1e-22", 1e-22},
        {"1e-999", 0.0},
        {"0e999999999", 0.0},
    };
    for (size_t k = 0; k < sizeof(k_cases) / sizeof(k_cases[0]); ++k) {
        sprat_error error;
        sprat_reader r = make_reader(k_cases[k].text, &error);
        double d = -1;
        if (!sprat_reader_double(&r, &d)) {
            FAIL_AT("\"%s\" did not parse: %s", k_cases[k].text, error.message);
        } else if (d != k_cases[k].value) {
            FAIL_AT("\"%s\" parsed as %.17g, expected %.17g", k_cases[k].text, d, k_cases[k].value);
        }
    }
    /* Exponents past 22 take several roundings: within a few ULPs. */
    sprat_error error;
    double d = 0;
    sprat_reader r = make_reader("1e300 1e-300 1e999", &error);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d / 1e300 > 1 - 1e-15 && d / 1e300 < 1 + 1e-15);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d / 1e-300 > 1 - 1e-15 && d / 1e-300 < 1 + 1e-15);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d > DBL_MAX); /* infinity, which the loader then rejects */

    /* More than 19 significant digits: the rest only scale the value. */
    r = make_reader("12345678901234567890123 0.12345678901234567890123", &error);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d > 1.2345678901234e22 && d < 1.2345678901235e22);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d > 0.12345678901234 && d < 0.12345678901235);

    /* However many digits are dropped, the value keeps its scale: a fraction
     * with 200000 more digits is still about 0.123, not 0. */
    size_t extra = 200000;
    const char* head = "0.1234567890123456789";
    size_t head_len = strlen(head);
    char* text = malloc(head_len + extra + 1);
    CHECK(text != NULL);
    if (text == NULL) return;
    memcpy(text, head, head_len);
    memset(text + head_len, '7', extra);
    text[head_len + extra] = '\0';
    r = make_reader(text, &error);
    CHECK(sprat_reader_double(&r, &d));
    CHECK(d > 0.12345678901234 && d < 0.12345678901235);
    free(text);
}

static void test_bad_doubles(void) {
    static const char* const k_texts[] = {"1.", ".5", "1e", "1e+", "1e-", "nan", "inf", "1.5x",
                                          "1,5", "1.5.5", "--1", "", "e5", "1e5.5"};
    for (size_t k = 0; k < sizeof(k_texts) / sizeof(k_texts[0]); ++k) {
        sprat_error error;
        sprat_reader r = make_reader(k_texts[k], &error);
        double d = 7;
        char expected[96];
        if (k_texts[k][0] == '\0') {
            snprintf(expected, sizeof(expected), "expected a number, found the end of the file");
        } else {
            snprintf(expected, sizeof(expected), "expected a number, found \"%s\"", k_texts[k]);
        }
        CHECK_FAILED(r, error, sprat_reader_double(&r, &d), SPRAT_ERR_SYNTAX, 1, 1, expected);
        CHECK_DOUBLE(d, 7);
    }
}

static void test_counts(void) {
    sprat_error error;
    /* Each item needs at least two more bytes. */
    sprat_reader r = make_reader("2 a b", &error);
    size_t n = 0;
    CHECK(sprat_reader_count(&r, &n));
    CHECK_SIZE(n, 2);
    r = make_reader("0", &error);
    CHECK(sprat_reader_count(&r, &n));
    CHECK_SIZE(n, 0);
    r = make_reader("3 a b", &error);
    CHECK_FAILED(r, error, sprat_reader_count(&r, &n), SPRAT_ERR_SYNTAX, 1, 1,
                 "count is more than the rest of the file can hold, found \"3\"");
    r = make_reader(" -1 a b", &error);
    CHECK_FAILED(r, error, sprat_reader_count(&r, &n), SPRAT_ERR_SYNTAX, 1, 2,
                 "expected a count (0 or more), found \"-1\"");
    r = make_reader("x", &error);
    CHECK_FAILED(r, error, sprat_reader_count(&r, &n), SPRAT_ERR_SYNTAX, 1, 1,
                 "expected an integer, found \"x\"");
}

static void test_tags(void) {
    sprat_error error;
    sprat_reader r = make_reader("  atlas\tsprite", &error);
    CHECK(sprat_reader_tag(&r, "atlas"));
    CHECK_SIZE(r.token_start, 2);
    CHECK_FAILED(r, error, sprat_reader_tag(&r, "sprit"), SPRAT_ERR_SYNTAX, 1, 9,
                 "expected \"sprit\", found \"sprite\"");
    r = make_reader("", &error);
    CHECK_FAILED(r, error, sprat_reader_tag(&r, "anim"), SPRAT_ERR_SYNTAX, 1, 1,
                 "expected \"anim\", found the end of the file");
}

static void test_strings(void) {
    sprat_error error;
    /* The last string ends at the end of the input, with no separator after it. */
    sprat_reader r = make_reader("0: 3:a b\n2:\xC3\xA9 4:\xF0\x9F\x90\x9F 1:z", &error);
    const char* s = NULL;
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "");
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "a b");
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "\xC3\xA9");
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "\xF0\x9F\x90\x9F");
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "z");
    CHECK(sprat_reader_end(&r));
    /* Strings may hold any whitespace, and a length may have leading zeros. */
    r = make_reader("004:\t\r\n  ", &error);
    CHECK(sprat_reader_string(&r, &s));
    CHECK_STR(s, "\t\r\n ");
}

static void test_bad_strings(void) {
    static const struct {
        const char* text;
        size_t column;
        const char* message;
    } k_cases[] = {
        {"abc", 1, "expected a string, found \"abc\""},
        {"3", 1, "expected a string, found \"3\""},
        {":a", 1, "expected a string, found \":a\""},
        {"-1:a", 1, "expected a string, found \"-1:a\""},
        {"", 1, "expected a string, found the end of the file"},
        {"5:abc", 1, "string runs past the end of the file, found \"5:abc\""},
        {"99999999999999999999:a", 1,
         "string runs past the end of the file, found \"99999999999999999999:a\""},
        {"1:ab", 4, "expected a space after the string, found \"b\""},
        /* A truncated multi-byte sequence is invalid, even when the length fits. */
        {"1:\xC3 x", 3, "invalid UTF-8 in string, found \"\\xC3\""},
        /* The separator is checked first: one byte of "é" leaves the other. */
        {"1:\xC3\xA9", 4, "expected a space after the string, found \"\\xA9\""},
        {"1:\x80", 3, "invalid UTF-8 in string, found \"\\x80\""},
        {"2:\xC0\x80", 3, "invalid UTF-8 in string, found \"\\xC0\\x80\""},       /* overlong NUL */
        {"3:\xE0\x80\xAF", 3, "invalid UTF-8 in string, found \"\\xE0\\x80\\xAF\""}, /* overlong */
        {"4:\xF0\x80\x80\xAF", 3,
         "invalid UTF-8 in string, found \"\\xF0\\x80\\x80\\xAF\""},                /* overlong */
        {"3:\xED\xA0\x80", 3, "invalid UTF-8 in string, found \"\\xED\\xA0\\x80\""}, /* surrogate */
        {"4:\xF4\x90\x80\x80", 3,
         "invalid UTF-8 in string, found \"\\xF4\\x90\\x80\\x80\""},                /* > U+10FFFF */
        {"1:\xF5", 3, "invalid UTF-8 in string, found \"\\xF5\""},
        {"2:\xC3(", 3, "invalid UTF-8 in string, found \"\\xC3(\""},
        {"1:\xFF", 3, "invalid UTF-8 in string, found \"\\xFF\""},
    };
    for (size_t k = 0; k < sizeof(k_cases) / sizeof(k_cases[0]); ++k) {
        sprat_error error;
        sprat_reader r = make_reader(k_cases[k].text, &error);
        const char* s = NULL;
        CHECK_FAILED(r, error, sprat_reader_string(&r, &s), SPRAT_ERR_SYNTAX, 1,
                     k_cases[k].column, k_cases[k].message);
    }
    /* NUL bytes would cut the copy short; the input needs its length here. */
    static const char k_nul[] = "3:a\0b";
    sprat_error error;
    memset(&error, 0, sizeof(error));
    sprat_reader r = {k_nul, sizeof(k_nul) - 1, 0, 0, &error, g_pool, sizeof(g_pool)};
    const char* s = NULL;
    CHECK_FAILED(r, error, sprat_reader_string(&r, &s), SPRAT_ERR_SYNTAX, 1, 4,
                 "strings must not contain NUL bytes, found \"\\x00b\"");
}

static void test_input_is_not_modified(void) {
    /* Not NUL-terminated: only the first 8 bytes belong to the input. */
    static const char k_text[] = "2:ab 1:cX";
    sprat_error error;
    memset(&error, 0, sizeof(error));
    sprat_reader r = {k_text, 8, 0, 0, &error, g_pool, sizeof(g_pool)};
    const char* a = NULL;
    const char* c = NULL;
    CHECK(sprat_reader_string(&r, &a));
    CHECK_STR(a, "ab");
    CHECK(sprat_reader_string(&r, &c));
    CHECK_STR(c, "c");
    CHECK(sprat_reader_end(&r));
    CHECK(a < k_text || a >= k_text + sizeof(k_text));
    CHECK_STR(k_text, "2:ab 1:cX");
}

/* Each string takes its bytes plus a NUL, empty ones included; a string that
 * does not fit means the file declared fewer string bytes than it has. */
static void test_string_pool(void) {
    sprat_error error;
    sprat_reader r = make_reader("0: 2:ab\n  1:c", &error);
    r.strings_left = 4; /* "" and "ab" with their NULs */
    const char* empty = NULL;
    const char* ab = NULL;
    const char* c = NULL;
    CHECK(sprat_reader_string(&r, &empty));
    CHECK_STR(empty, "");
    CHECK(sprat_reader_string(&r, &ab));
    CHECK_STR(ab, "ab");
    CHECK(ab == empty + 1);
    CHECK_SIZE(r.strings_left, 0);
    CHECK_FAILED(r, error, sprat_reader_string(&r, &c), SPRAT_ERR_SCHEMA, 2, 3,
                 "strings exceed the string byte total on the sheet line");
}

static void test_end(void) {
    sprat_error error;
    sprat_reader r = make_reader(" \t\r\n ", &error);
    CHECK(sprat_reader_end(&r));
    r = make_reader("", &error);
    CHECK(sprat_reader_end(&r));
    r = make_reader("\n  extra stuff", &error);
    CHECK_FAILED(r, error, sprat_reader_end(&r), SPRAT_ERR_SYNTAX, 2, 3,
                 "unexpected data after the last record, found \"extra\"");
}

/* Lines start after \n; columns count code points, not bytes. */
static void test_positions(void) {
    sprat_error error;
    sprat_reader r = make_reader("tag\n  \xC3\xA9 x", &error);
    int i;
    CHECK(sprat_reader_tag(&r, "tag"));
    CHECK_FAILED(r, error, sprat_reader_int(&r, &i), SPRAT_ERR_SYNTAX, 2, 3,
                 "expected an integer, found \"\xC3\xA9\"");
    CHECK_SIZE(r.token_start, 6);

    size_t line = 0;
    size_t column = 0;
    r = make_reader("ab\r\n\xE2\x9C\x93x\n", &error);
    sprat_reader_locate(&r, 0, &line, &column);
    CHECK(line == 1 && column == 1);
    sprat_reader_locate(&r, 3, &line, &column); /* the \n itself is on line 1 */
    CHECK(line == 1 && column == 4);
    sprat_reader_locate(&r, 7, &line, &column); /* "x", after a 3-byte code point */
    CHECK(line == 2 && column == 2);
    sprat_reader_locate(&r, 100, &line, &column); /* past the end: the end */
    CHECK(line == 3 && column == 1);
}

static void test_quote(void) {
    char out[64];
    sprat_quote(out, sizeof(out), "plain", 5);
    CHECK_STR(out, "\"plain\"");
    sprat_quote(out, sizeof(out), "", 0);
    CHECK_STR(out, "\"\"");
    sprat_quote(out, sizeof(out), "a\"b\\c\nd\te\x7F", 10);
    CHECK_STR(out, "\"a\\\"b\\\\c\\nd\\x09e\\x7F\"");
    /* Valid UTF-8 is kept; invalid bytes are escaped one by one. */
    sprat_quote(out, sizeof(out), "\xC3\xA9\xC3", 3);
    CHECK_STR(out, "\"\xC3\xA9\\xC3\"");
    /* Code points that steer terminals or reorder text are escaped: C1
     * controls (U+0080..U+009F) and bidirectional marks and overrides. */
    sprat_quote(out, sizeof(out), "\xC2\x80\xC2\x9B[2J\xC2\x9F\xC2\xA0", 11);
    CHECK_STR(out, "\"\\u0080\\u009B[2J\\u009F\xC2\xA0\"");
    sprat_quote(out, sizeof(out),
                "\xE2\x80\x8E\xE2\x80\x8F\xE2\x80\xAA\xE2\x80\xAE\xE2\x81\xA6\xE2\x81\xA9"
                "\xE2\x80\x8D\xE2\x81\xAA", 24);
    CHECK_STR(out, "\"\\u200E\\u200F\\u202A\\u202E\\u2066\\u2069\xE2\x80\x8D\xE2\x81\xAA\"");
    /* An escape is never cut in half either. */
    sprat_quote(out, 12, "abcd\xE2\x80\xAE", 7);
    CHECK_STR(out, "\"abcd...\"");
    /* Only len bytes are read. */
    sprat_quote(out, sizeof(out), "abcdef", 3);
    CHECK_STR(out, "\"abc\"");
    /* Cut with "..." to fit, never in the middle of an escape or a code point. */
    sprat_quote(out, 12, "abcdefghijklmnop", 16);
    CHECK_STR(out, "\"abcdef...\"");
    sprat_quote(out, 12, "abcde\n", 6);
    CHECK_STR(out, "\"abcde...\"");
    sprat_quote(out, 12, "abcde\xE2\x9C\x93", 8);
    CHECK_STR(out, "\"abcde...\"");
    /* Buffers too small for a quote get an empty string, or nothing at all. */
    sprat_quote(out, 7, "abc", 3);
    CHECK_STR(out, "");
    out[0] = 'x';
    sprat_quote(out, 0, "abc", 3);
    CHECK(out[0] == 'x');
}

int main(void) {
    RUN(test_ints);
    RUN(test_bad_ints);
    RUN(test_doubles);
    RUN(test_bad_doubles);
    RUN(test_counts);
    RUN(test_tags);
    RUN(test_strings);
    RUN(test_bad_strings);
    RUN(test_input_is_not_modified);
    RUN(test_string_pool);
    RUN(test_end);
    RUN(test_positions);
    RUN(test_quote);
    return TEST_RESULT();
}
