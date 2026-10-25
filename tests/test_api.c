/* The query and helper functions: lookups, sizes, path checks, status strings,
 * the version and the memory functions, with NULL and boundary arguments. */
#include "support.h"

/* Two sprites share a name in different folders, and two markers share a name. */
static const char* const k_sheet_text =
    HEADER(4, 3)
    SPRITE_AT("0 0 4 4", "2 4:walk 10:a/walk.png 0:")
    "marker 0 1 2 0 0 0 0 5:pivot\n"
    "marker 1 3 3 2 0 0 0 5:pivot\n"
    SPRITE_AT("4 0 4 4", "0 4:walk 10:b/walk.png 0:")
    SPRITE_AT("0 4 2 4", "0 0: 9:empty.png 0:")
    "sprite 0 4 4 4 2 90 1 2 3 4 0 0 0 0 0 0 0 0 0 0 4:turn 8:turn.png 0:\n"
    "anim 8 0 -1 2 0 1 3:run\n"
    "anim 8 0 -1 0 3:run\n"
    "anim 8 0 -1 0 0:\n";

static void test_find_sprite(void) {
    sprat_sheet* sheet = load_ok(k_sheet_text);
    if (sheet == NULL) return;
    CHECK(sprat_sheet_find_sprite(sheet, "walk") == &sheet->sprites[0]); /* the first of two */
    CHECK(sprat_sheet_find_sprite(sheet, "turn") == &sheet->sprites[3]);
    CHECK(sprat_sheet_find_sprite(sheet, "") == &sheet->sprites[2]);
    CHECK_NULL(sprat_sheet_find_sprite(sheet, "Walk")); /* case matters */
    CHECK_NULL(sprat_sheet_find_sprite(sheet, "wal"));
    CHECK_NULL(sprat_sheet_find_sprite(sheet, "walks"));
    CHECK_NULL(sprat_sheet_find_sprite(sheet, NULL));
    CHECK_NULL(sprat_sheet_find_sprite(NULL, "walk"));
    sprat_sheet_free(sheet);
}

static void test_find_sprite_by_path(void) {
    sprat_sheet* sheet = load_ok(k_sheet_text);
    if (sheet == NULL) return;
    CHECK(sprat_sheet_find_sprite_by_path(sheet, "a/walk.png") == &sheet->sprites[0]);
    CHECK(sprat_sheet_find_sprite_by_path(sheet, "b/walk.png") == &sheet->sprites[1]);
    CHECK_NULL(sprat_sheet_find_sprite_by_path(sheet, "walk.png"));
    CHECK_NULL(sprat_sheet_find_sprite_by_path(sheet, "walk")); /* a name, not a path */
    CHECK_NULL(sprat_sheet_find_sprite_by_path(sheet, ""));
    CHECK_NULL(sprat_sheet_find_sprite_by_path(sheet, NULL));
    CHECK_NULL(sprat_sheet_find_sprite_by_path(NULL, "a/walk.png"));
    sprat_sheet_free(sheet);
}

static void test_find_animation(void) {
    sprat_sheet* sheet = load_ok(k_sheet_text);
    if (sheet == NULL) return;
    CHECK(sprat_sheet_find_animation(sheet, "run") == &sheet->animations[0]); /* the first */
    CHECK(sprat_sheet_find_animation(sheet, "") == &sheet->animations[2]);
    CHECK_NULL(sprat_sheet_find_animation(sheet, "walk"));
    CHECK_NULL(sprat_sheet_find_animation(sheet, NULL));
    CHECK_NULL(sprat_sheet_find_animation(NULL, "run"));
    sprat_sheet_free(sheet);

    /* A sheet without animations has none to find. */
    sheet = load_ok(HEADER(0, 0));
    if (sheet == NULL) return;
    CHECK_SIZE(sheet->animation_count, 0);
    CHECK_NULL(sheet->animations);
    CHECK_NULL(sprat_sheet_find_animation(sheet, "run"));
    CHECK_NULL(sprat_sheet_find_sprite(sheet, "walk"));
    CHECK_NULL(sprat_sheet_find_sprite_by_path(sheet, "a/walk.png"));
    sprat_sheet_free(sheet);
}

static void test_find_marker(void) {
    sprat_sheet* sheet = load_ok(k_sheet_text);
    if (sheet == NULL) return;
    const sprat_sprite* walk = &sheet->sprites[0];
    const sprat_marker* pivot = sprat_sprite_find_marker(walk, "pivot");
    CHECK(pivot == &walk->markers[0]); /* the first of two */
    CHECK(pivot != NULL && pivot->type == SPRAT_MARKER_POINT);
    CHECK_NULL(sprat_sprite_find_marker(walk, "feet"));
    CHECK_NULL(sprat_sprite_find_marker(walk, NULL));
    CHECK_NULL(sprat_sprite_find_marker(NULL, "pivot"));
    /* A sprite without markers. */
    CHECK_NULL(sprat_sprite_find_marker(&sheet->sprites[1], "pivot"));
    sprat_sheet_free(sheet);
}

static void test_sizes(void) {
    sprat_sheet* sheet = load_ok(k_sheet_text);
    if (sheet == NULL) return;
    /* Unrotated and untrimmed: both are the rect. */
    sprat_size content = sprat_sprite_content_size(&sheet->sprites[2]);
    sprat_size source = sprat_sprite_source_size(&sheet->sprites[2]);
    CHECK_INT(content.w, 2);
    CHECK_INT(content.h, 4);
    CHECK_INT(source.w, 2);
    CHECK_INT(source.h, 4);
    /* Packed turned by 90: a 4x2 rect holds 2x4 content; trim 1 2 3 4 adds 4 and 6. */
    const sprat_sprite* turn = &sheet->sprites[3];
    CHECK_INT(turn->rect.w, 4);
    CHECK_INT(turn->rect.h, 2);
    content = sprat_sprite_content_size(turn);
    source = sprat_sprite_source_size(turn);
    CHECK_INT(content.w, 2);
    CHECK_INT(content.h, 4);
    CHECK_INT(source.w, 6);
    CHECK_INT(source.h, 10);
    sprat_sheet_free(sheet);

    /* 270 turns the same way as 90, as far as sizes go. */
    sheet = load_ok(HEADER(1, 0) "sprite 0 0 0 4 2 270 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n");
    if (sheet != NULL) {
        content = sprat_sprite_content_size(&sheet->sprites[0]);
        CHECK_INT(content.w, 2);
        CHECK_INT(content.h, 4);
    }
    sprat_sheet_free(sheet);

    content = sprat_sprite_content_size(NULL);
    source = sprat_sprite_source_size(NULL);
    CHECK_INT(content.w, 0);
    CHECK_INT(content.h, 0);
    CHECK_INT(source.w, 0);
    CHECK_INT(source.h, 0);
}

/* Sprites that never came from the loader are clamped, not overflowed. */
static void test_source_size_clamps(void) {
    sprat_sprite s;
    memset(&s, 0, sizeof(s));
    s.rect.w = 2147483647;
    s.rect.h = 1;
    s.trim.left = 2147483647;
    s.trim.bottom = 5;
    sprat_size source = sprat_sprite_source_size(&s);
    CHECK_INT(source.w, 2147483647);
    CHECK_INT(source.h, 6);
    s.rotation = 90; /* now the long side is the height */
    source = sprat_sprite_source_size(&s);
    CHECK_INT(source.w, 2147483647);
    CHECK_INT(source.h, 2147483647);
}

static void test_path_is_safe(void) {
    static const char* const k_safe[] = {
        "atlas.png", "a/b.png", "a\\b.png", "./x.png", "..a/b", "a..b/c..", ".hidden",
        "console.png", "com10.png", "comx.png", "nullable/a.png", "a b/c d.png", "a//b.png",
        "\xC3\xA9t\xC3\xA9.png", "lpt.png", "con_x", "dir/", "lptx.png", "COMa", "AUXI",
    };
    for (size_t i = 0; i < sizeof(k_safe) / sizeof(k_safe[0]); ++i) {
        if (!sprat_path_is_safe(k_safe[i])) FAIL_AT("\"%s\" should be safe", k_safe[i]);
    }
    static const char* const k_unsafe[] = {
        "", "/etc/passwd", "\\\\server\\share", "\\x.png", "C:\\x.png", "C:x.png", "a:b",
        "x.png:stream", "../x.png", "a/../x.png", "a\\..\\x.png", "a/..", "..", "x\n.png",
        "x\x1B.png", "x\x7F.png", "x\t.png",
        /* Windows trims trailing dots and spaces, and opens devices by name. */
        ".. ", ".. /x.png", "...", "a/. ./x.png", " ", "CON", "nul.png", "a/Aux.txt",
        "COM1.png", "com0", "lpt9", "con .png", "conout$", "CONIN$.txt", "prn/x.png",
    };
    for (size_t i = 0; i < sizeof(k_unsafe) / sizeof(k_unsafe[0]); ++i) {
        if (sprat_path_is_safe(k_unsafe[i])) FAIL_AT("\"%s\" should be unsafe", k_unsafe[i]);
    }
    CHECK(!sprat_path_is_safe(NULL));
}

static void test_status_string(void) {
    CHECK_STR(sprat_status_string(SPRAT_OK), "ok");
    CHECK_STR(sprat_status_string(SPRAT_ERR_INVALID_ARG), "invalid argument");
    CHECK_STR(sprat_status_string(SPRAT_ERR_IO), "I/O error");
    CHECK_STR(sprat_status_string(SPRAT_ERR_NOMEM), "out of memory");
    CHECK_STR(sprat_status_string(SPRAT_ERR_SYNTAX), "syntax error");
    CHECK_STR(sprat_status_string(SPRAT_ERR_SCHEMA), "invalid sprat metadata");
    CHECK_STR(sprat_status_string(SPRAT_ERR_NOT_FOUND), "not found");
    CHECK_STR(sprat_status_string(SPRAT_ERR_AMBIGUOUS), "ambiguous name");
    CHECK_STR(sprat_status_string((sprat_status)99), "unknown status");
    CHECK_STR(sprat_status_string((sprat_status)-1), "unknown status");
}

/* The version comes from the VERSION file. */
static void test_version(void) {
    CHECK_STR(sprat_version(), SPRAT_EXPECTED_VERSION);
    CHECK(sprat_version()[0] == 'v');
}

/* Setting only some functions is a caller bug: nothing changes. */
static void test_memory_functions(void) {
    sprat_malloc_func default_m = NULL;
    sprat_calloc_func default_c = NULL;
    sprat_realloc_func default_r = NULL;
    sprat_free_func default_f = NULL;
    sprat_get_memory_functions(&default_m, &default_c, &default_r, &default_f);
    CHECK(default_m != NULL && default_c != NULL && default_r != NULL && default_f != NULL);

    /* Each one missing in turn. */
    CHECK(!sprat_set_memory_functions(NULL, counting_calloc, counting_realloc, counting_free));
    CHECK(!sprat_set_memory_functions(counting_malloc, NULL, counting_realloc, counting_free));
    CHECK(!sprat_set_memory_functions(counting_malloc, counting_calloc, NULL, counting_free));
    CHECK(!sprat_set_memory_functions(counting_malloc, counting_calloc, counting_realloc, NULL));
    CHECK(!sprat_set_memory_functions(NULL, NULL, NULL, NULL));
    sprat_malloc_func m = NULL;
    sprat_calloc_func c = NULL;
    sprat_realloc_func r = NULL;
    sprat_free_func f = NULL;
    sprat_get_memory_functions(&m, &c, &r, &f);
    CHECK(m == default_m && c == default_c && r == default_r && f == default_f);

    use_counting(0);
    /* Any argument may be NULL. */
    sprat_get_memory_functions(NULL, NULL, NULL, NULL);
    m = NULL;
    f = NULL;
    sprat_get_memory_functions(&m, NULL, NULL, &f);
    CHECK(m == counting_malloc && f == counting_free);
    c = NULL;
    r = NULL;
    sprat_get_memory_functions(NULL, &c, &r, NULL);
    CHECK(c == counting_calloc && r == counting_realloc);

    /* Loads use the functions that are set; the defaults can be put back. */
    sprat_sheet* sheet = load_ok(HEADER(0, 0));
    CHECK(g_counting.allocations == 1 && g_counting.live == 1);
    sprat_sheet_free(sheet);
    CHECK_INT(g_counting.live, 0);
    CHECK(sprat_set_memory_functions(default_m, default_c, default_r, default_f));
}

/* A sheet frees with the functions it was loaded with, not the current ones. */
static void test_free_uses_load_functions(void) {
    use_counting(0);
    sprat_sheet* sheet = load_ok(HEADER(0, 0));
    CHECK_INT(g_counting.live, 1);
    use_c_library();
    sprat_sheet_free(sheet);
    CHECK_INT(g_counting.live, 0);
    sprat_sheet_free(NULL);
}

int main(void) {
    RUN(test_find_sprite);
    RUN(test_find_sprite_by_path);
    RUN(test_find_animation);
    RUN(test_find_marker);
    RUN(test_sizes);
    RUN(test_source_size_clamps);
    RUN(test_path_is_safe);
    RUN(test_status_string);
    RUN(test_version);
    RUN(test_memory_functions);
    RUN(test_free_uses_load_functions);
    return TEST_RESULT();
}
