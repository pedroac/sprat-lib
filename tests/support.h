/* Helpers shared by the sheet tests: sheets written by hand, a counting
 * allocator that can fail on demand, and error checks that print the whole
 * error when it is not the one expected. */
#ifndef SPRAT_TEST_SUPPORT_H
#define SPRAT_TEST_SUPPORT_H

#include "sprat/sprat.h"
#include "test.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Each test file uses some of these helpers. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* tests/fixtures/multipack.sprat comes from sprat-cli; see tests/fixtures/README.md. */
#define MULTIPACK_FIXTURE SPRAT_FIXTURES_DIR "/multipack.sprat"

/* sprat_sheet_load_file()'s allocations besides the read buffer and the sheet:
 * on Windows, the path widened to UTF-16 for _wfopen(). */
#if defined(_WIN32)
#define PATH_ALLOCATIONS 1
#else
#define PATH_ALLOCATIONS 0
#endif

/* ---- Counting memory functions ----------------------------------------- */

/* Memory functions that track balance and can fail the Nth allocation. */
typedef struct counting {
    long live;        /* blocks allocated and not yet freed */
    long allocations; /* malloc, calloc and realloc calls */
    long fail_at;     /* 1-based; 0 never fails */
} counting;

static counting g_counting;

static bool counting_fails(void) {
    ++g_counting.allocations;
    return g_counting.fail_at != 0 && g_counting.allocations == g_counting.fail_at;
}

static void* counting_malloc(size_t size) {
    if (counting_fails()) return NULL;
    void* p = malloc(size);
    if (p != NULL) ++g_counting.live;
    return p;
}

static void* counting_calloc(size_t count, size_t size) {
    if (counting_fails()) return NULL;
    void* p = calloc(count, size);
    if (p != NULL) ++g_counting.live;
    return p;
}

static void* counting_realloc(void* ptr, size_t size) {
    if (counting_fails()) return NULL;
    void* p = realloc(ptr, size);
    if (p != NULL && ptr == NULL) ++g_counting.live;
    return p;
}

static void counting_free(void* ptr) {
    if (ptr != NULL) --g_counting.live;
    free(ptr);
}

static void use_counting(long fail_at) {
    counting fresh = {0, 0, fail_at};
    g_counting = fresh;
    CHECK(sprat_set_memory_functions(counting_malloc, counting_calloc, counting_realloc,
                                     counting_free));
}

static void use_c_library(void) {
    CHECK(sprat_set_memory_functions(malloc, calloc, realloc, free));
}

/* ---- Sheets written by hand -------------------------------------------- */

/* A sheet header: one 8x8 atlas, then `sprites` sprite and `anims` animation
 * records. "~" stands for the sheet line's totals; see with_totals(). */
#define HEADER_FMT "sprat-raw 1\nsheet 1 0 0 0 1 %d %d 0 0 %d %d 0: 0:\natlas 8 8 0: 0: 0:\n"
#define HEADER(sprites, anims) \
    "sprat-raw 1\nsheet 1 0 0 0 1 " #sprites " " #anims " ~ 0: 0:\natlas 8 8 0: 0: 0:\n"
/* The same in version 2, whose anim records have a LOOP after FLIP. */
#define HEADER_V2(sprites, anims) \
    "sprat-raw 2\nsheet 1 0 0 0 1 " #sprites " " #anims " ~ 0: 0:\natlas 8 8 0: 0: 0:\n"
/* A sprite record on atlas 0; `rect` and `tail` (MARKERS NAME PATH ALIAS_OF)
 * replace parts of it. Every other value is 0. */
#define SPRITE_AT(rect, tail) "sprite 0 " rect " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 " tail "\n"
#define SPRITE SPRITE_AT("0 0 4 4", "0 1:a 5:a.png 0:")

/* Token at the front of *p: skips leading whitespace, returns its start and
 * length, and moves *p past it. A string token ("N:bytes") takes N bytes,
 * spaces and all; *string_bytes adds them up. */
static const char* next_token(const char** p, size_t* len, size_t* string_bytes) {
    const char* s = *p;
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') ++s;
    const char* start = s;
    const char* digits = s;
    while (*digits >= '0' && *digits <= '9') ++digits;
    if (digits > s && *digits == ':') {
        size_t n = (size_t)strtoul(s, NULL, 10);
        size_t avail = strlen(digits + 1);
        n = n < avail ? n : avail;
        *string_bytes += n;
        s = digits + 1 + n;
    } else {
        while (*s != '\0' && *s != ' ' && *s != '\n' && *s != '\r' && *s != '\t') ++s;
    }
    *len = (size_t)(s - start);
    *p = s;
    return start;
}

/* Test sheets write "~" for the sheet line's MARKERS VERTICES FRAMES STRING_BYTES;
 * this fills in what the records actually hold. Tests of wrong totals write
 * them out instead. Only the sheet line changes, so positions on later lines
 * stay as written. */
static const char* with_totals(const char* text) {
    static char out[8192];
    const char* mark = strstr(text, " ~ ");
    if (mark == NULL) return text;
    size_t markers = 0, vertices = 0, frames = 0, string_bytes = 0;
    /* FRAMES is an anim record's 4th value, or its 5th from version 2 on (after LOOP). */
    const size_t frames_index = strncmp(text, "sprat-raw 1", 11) == 0 ? 4 : 5;
    const char* p = text;
    size_t index = 0; /* of the token within its record */
    bool in_marker = false;
    bool in_anim = false;
    while (*p != '\0') {
        size_t len;
        const char* tok = next_token(&p, &len, &string_bytes);
        if (len == 0) break;
        if (len == 6 && strncmp(tok, "marker", 6) == 0) {
            ++markers;
            in_marker = true;
            in_anim = false;
            index = 0;
        } else if ((len == 4 && strncmp(tok, "anim", 4) == 0)
                   || (len == 6 && strncmp(tok, "sprite", 6) == 0)
                   || (len == 5 && strncmp(tok, "atlas", 5) == 0)) {
            in_marker = false;
            in_anim = tok[0] == 'a' && tok[1] == 'n';
            index = 0;
        } else {
            ++index;
            if (in_marker && index == 7) vertices += (size_t)strtoul(tok, NULL, 10);
            if (in_anim && index == frames_index) frames += (size_t)strtoul(tok, NULL, 10);
        }
    }
    int n = snprintf(out, sizeof(out), "%.*s %zu %zu %zu %zu%s", (int)(mark - text), text,
                     markers, vertices, frames, string_bytes, mark + 2);
    if (n < 0 || (size_t)n >= sizeof(out)) abort();
    return out;
}

static sprat_status load_text(const char* text, sprat_sheet** sheet, sprat_error* error) {
    text = with_totals(text);
    return sprat_sheet_load_memory(text, strlen(text), sheet, error);
}

/* Loads text that must succeed; prints the error when it does not. */
static sprat_sheet* load_ok_at(const char* file, int line, const char* text) {
    sprat_sheet* sheet = NULL;
    sprat_error error;
    if (load_text(text, &sheet, &error) != SPRAT_OK) {
        fprintf(stderr, "%s:%d: load failed: %s (%s, line %zu column %zu)\n", file, line,
                error.message, sprat_status_string(error.status), error.line, error.column);
        ++g_failures;
    }
    return sheet;
}
#define load_ok(text) load_ok_at(__FILE__, __LINE__, (text))

/* Checks a failed load: the status, the exact message, and the position
 * (line 0 for errors without one), and that *out was reset to NULL. */
static void check_error_at(const char* file, int line, sprat_status status,
                           const sprat_error* error, sprat_sheet* sheet, sprat_status expected,
                           size_t expected_line, size_t expected_column,
                           const char* expected_message) {
    bool ok = status == expected && error->status == status && sheet == NULL
           && strcmp(error->message, expected_message) == 0 && error->line == expected_line
           && error->column == expected_column;
    if (!ok) {
        fprintf(stderr, "%s:%d: wrong error\n", file, line);
        fprintf(stderr, "    got:      %s (returned %s), line %zu column %zu, sheet %s\n"
                        "              \"%s\"\n",
                sprat_status_string(error->status), sprat_status_string(status), error->line,
                error->column, sheet == NULL ? "NULL" : "set", error->message);
        fprintf(stderr, "    expected: %s, line %zu column %zu\n              \"%s\"\n",
                sprat_status_string(expected), expected_line, expected_column, expected_message);
        ++g_failures;
    }
}

static void expect_error_at(const char* file, int line, const char* text, sprat_status expected,
                            size_t expected_line, size_t expected_column,
                            const char* expected_message) {
    sprat_sheet* sheet = (sprat_sheet*)&sheet; /* must be reset to NULL */
    sprat_error error;
    sprat_status status = load_text(text, &sheet, &error);
    check_error_at(file, line, status, &error, sheet, expected, expected_line, expected_column,
                   expected_message);
    if (status == SPRAT_OK) sprat_sheet_free(sheet);
}

/* expect_error(text, status, line, column, message) */
#define expect_error(...) expect_error_at(__FILE__, __LINE__, __VA_ARGS__)

/* The whole fixture, NUL-terminated. Free with free(). */
static char* read_fixture(size_t* size) {
    FILE* f = fopen(MULTIPACK_FIXTURE, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", MULTIPACK_FIXTURE);
        abort();
    }
    char* text = malloc(65536);
    if (text == NULL) abort();
    *size = fread(text, 1, 65535, f);
    text[*size] = '\0';
    fclose(f);
    return text;
}

#endif /* SPRAT_TEST_SUPPORT_H */
