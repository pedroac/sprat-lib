/* sprat_sheet_load_file(): arguments, missing and unreadable files, empty and
 * large files, UTF-8 names, pipes (POSIX), and failing allocations. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "support.h"

#include <errno.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define TMP_PATH(name) SPRAT_TEST_TMP_DIR "/" name

/* fopen() for a UTF-8 path, the way the library opens it. */
static FILE* open_utf8(const char* path, const char* mode) {
#if defined(_WIN32)
    wchar_t wide_path[512];
    wchar_t wide_mode[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, 512) == 0) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8) == 0) return NULL;
    return _wfopen(wide_path, wide_mode);
#else
    return fopen(path, mode);
#endif
}

static void remove_utf8(const char* path) {
#if defined(_WIN32)
    wchar_t wide_path[512];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, 512) != 0) _wremove(wide_path);
#else
    remove(path);
#endif
}

/* Writes text followed by `padding` newlines, which the loader skips. */
static bool write_file(const char* path, const char* text, size_t size, size_t padding) {
    FILE* f = open_utf8(path, "wb");
    if (f == NULL) {
        FAIL_AT("cannot create %s", path);
        return false;
    }
    bool ok = fwrite(text, 1, size, f) == size;
    for (size_t i = 0; i < padding && ok; ++i) ok = fputc('\n', f) != EOF;
    ok = fclose(f) == 0 && ok;
    if (!ok) FAIL_AT("cannot write %s", path);
    return ok;
}

static void expect_file_error(const char* file, int line, const char* path, sprat_status expected,
                              const char* expected_message) {
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    sprat_status status = sprat_sheet_load_file(path, &sheet, &error);
    check_error_at(file, line, status, &error, sheet, expected, 0, 0, expected_message);
}

static void test_fixture(void) {
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, &error), SPRAT_OK);
    CHECK_INT(error.status, SPRAT_OK);
    CHECK_STR(error.message, "");
    CHECK_NOT_NULL(sheet);
    if (sheet != NULL) CHECK_SIZE(sheet->sprite_count, 4);
    sprat_sheet_free(sheet);
    /* The error is optional. */
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    sprat_sheet_free(sheet);
}

static void test_arguments(void) {
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    sprat_status status = sprat_sheet_load_file(NULL, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "path must not be NULL");
    status = sprat_sheet_load_file(MULTIPACK_FIXTURE, NULL, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "out must not be NULL");
    status = sprat_sheet_load_file(NULL, NULL, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "path and out must not be NULL");
    CHECK_INT(sprat_sheet_load_file(NULL, NULL, NULL), SPRAT_ERR_INVALID_ARG);
}

/* The message quotes the path and gives the C library's reason. */
static void test_missing_file(void) {
    const char* path = TMP_PATH("does-not-exist.sprat");
    char expected[256];
    snprintf(expected, sizeof(expected), "cannot open \"%s\": %s", path, strerror(ENOENT));
    expect_file_error(__FILE__, __LINE__, path, SPRAT_ERR_IO, expected);

    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file("", &sheet, &error), SPRAT_ERR_IO);
    CHECK_NULL(sheet);
    CHECK(strncmp(error.message, "cannot open \"\": ", 16) == 0);
}

/* A directory opens on Linux and reports a length of about 2^63; it must fail
 * as an I/O error, not as a request for that much memory. */
static void test_directory(void) {
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file(SPRAT_FIXTURES_DIR, &sheet, &error), SPRAT_ERR_IO);
    CHECK_NULL(sheet);
    CHECK(strncmp(error.message, "cannot ", 7) == 0);
    CHECK(strstr(error.message, SPRAT_FIXTURES_DIR) != NULL);
}

/* File contents go through the same checks as memory, positions included. */
static void test_bad_contents(void) {
    const char* path = TMP_PATH("empty.sprat");
    if (!write_file(path, "", 0, 0)) return;
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    sprat_status status = sprat_sheet_load_file(path, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SYNTAX, 1, 1,
                   "not a sprat raw file: expected \"sprat-raw\", found the end of the file");
    remove(path);

    path = TMP_PATH("bad.sprat");
    static const char k_bad[] = "sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 0 0: 0:\natlas 8 0 0: 0: 0:\n";
    if (!write_file(path, k_bad, sizeof(k_bad) - 1, 0)) return;
    status = sprat_sheet_load_file(path, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SCHEMA, 3, 9,
                   "atlases[0].height: must be at least 1, found 0");
    remove(path);
}

/* Longer than the first 64 KiB read buffer when the length is unknown, and
 * read in one go when it is known. */
static void test_large_file(void) {
    size_t size;
    char* text = read_fixture(&size);
    const char* path = TMP_PATH("large.sprat");
    if (write_file(path, text, size, 300000)) {
        use_counting(0);
        sprat_sheet* sheet = NULL;
        sprat_error error;
        CHECK_INT(sprat_sheet_load_file(path, &sheet, &error), SPRAT_OK);
        /* the read buffer and the sheet */
        CHECK_INT(g_counting.allocations, 2 + PATH_ALLOCATIONS);
        sprat_sheet_free(sheet);
        CHECK_INT(g_counting.live, 0);
        use_c_library();
        remove(path);
    }
    free(text);
}

/* Paths are UTF-8 on every platform, Windows included. */
static void test_utf8_path(void) {
    size_t size;
    char* text = read_fixture(&size);
    const char* path = TMP_PATH("sprat \xC3\xA9t\xC3\xA9 \xE2\x9C\x93.sprat");
    if (write_file(path, text, size, 0)) {
        sprat_sheet* sheet = NULL;
        sprat_error error;
        CHECK_INT(sprat_sheet_load_file(path, &sheet, &error), SPRAT_OK);
        if (sheet == NULL) fprintf(stderr, "  %s\n", error.message);
        sprat_sheet_free(sheet);
        remove_utf8(path);
    }
    free(text);
}

/* Long paths are cut in the message, which always ends in a NUL. */
static void test_long_path(void) {
    char path[600];
    memset(path, 'p', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file(path, &sheet, &error), SPRAT_ERR_IO);
    CHECK(memchr(error.message, '\0', sizeof(error.message)) != NULL);
    CHECK(strncmp(error.message, "cannot open \"ppppp", 18) == 0);
    CHECK(strstr(error.message, "...\"") != NULL);
}

#if !defined(_WIN32)
/* A pipe has no length, so the buffer starts at 64 KiB and grows. */
static void test_pipe(void) {
    size_t size;
    char* text = read_fixture(&size);
    const char* path = TMP_PATH("pipe.sprat");
    remove(path);
    if (mkfifo(path, 0600) != 0) {
        FAIL_AT("mkfifo %s: %s", path, strerror(errno));
        free(text);
        return;
    }
    pid_t child = fork();
    if (child == 0) {
        FILE* f = fopen(path, "wb");
        if (f == NULL) _exit(1);
        fwrite(text, 1, size, f);
        for (int i = 0; i < 200000; ++i) fputc(' ', f); /* past two doublings */
        fclose(f);
        _exit(0);
    }
    use_counting(0);
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file(path, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) fprintf(stderr, "  %s\n", error.message);
    CHECK(g_counting.allocations >= 4); /* 64 KiB, grown at least twice, then the sheet */
    sprat_sheet_free(sheet);
    CHECK_INT(g_counting.live, 0);
    use_c_library();
    int child_status = 0;
    waitpid(child, &child_status, 0);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    remove(path);
    free(text);
}
#endif

/* Fails each allocation in turn: every failure must report NOMEM and leak nothing. */
static void test_out_of_memory(void) {
    use_counting(0);
    sprat_sheet* sheet = NULL;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    sprat_sheet_free(sheet);
    long total = g_counting.allocations;
    CHECK(total > 1);

    for (long n = 1; n <= total; ++n) {
        use_counting(n);
        sprat_error error;
        sheet = (sprat_sheet*)&sheet;
        sprat_status status = sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, &error);
        if (status != SPRAT_ERR_NOMEM || sheet != NULL || g_counting.live != 0) {
            FAIL_AT("allocation %ld of %ld failing: %s, sheet %s, %ld blocks leaked: %s", n, total,
                    sprat_status_string(status), sheet == NULL ? "NULL" : "set", g_counting.live,
                    error.message);
        }
    }
    use_c_library();
}

int main(void) {
    RUN(test_fixture);
    RUN(test_arguments);
    RUN(test_missing_file);
    RUN(test_directory);
    RUN(test_bad_contents);
    RUN(test_large_file);
    RUN(test_utf8_path);
    RUN(test_long_path);
#if !defined(_WIN32)
    RUN(test_pipe);
#endif
    RUN(test_out_of_memory);
    return TEST_RESULT();
}
