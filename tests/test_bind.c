/* sprat_sheet_bind(): names to indexes, all or none, with exact messages for
 * every way a binding can fail. */
#include "support.h"

/* Two sprites share the name "walk"; "fly" is only an animation and "jump"
 * only a sprite, for the hints. */
static const char* const k_text =
    HEADER(4, 3)
    SPRITE_AT("0 0 4 4", "0 4:walk 10:a/walk.png 0:")
    SPRITE_AT("4 0 4 4", "0 4:walk 10:b/walk.png 0:")
    SPRITE_AT("0 4 4 4", "0 4:idle 8:idle.png 0:")
    SPRITE_AT("4 4 4 4", "0 4:jump 10:x/jump.png 0:")
    "anim 8 0 -1 2 0 1 3:run\n"
    "anim 8 1 0 0 8:run-left\n"
    "anim 4 0 -1 1 2 3:fly\n";

/* Binds one name, expecting it to fail with this status and message. */
static void expect_bind_error_at(const char* file, int line, const sprat_sheet* sheet,
                                 sprat_bind_kind kind, const char* name, unsigned flags,
                                 sprat_status expected, const char* expected_message) {
    size_t index = 777;
    sprat_binding b = {kind, name, &index, flags};
    sprat_error error;
    sprat_status status = sprat_sheet_bind(sheet, &b, 1, &error);
    check_error_at(file, line, status, &error, NULL, expected, 0, 0, expected_message);
    if (index != 777) FAIL_AT("index was written on failure: %zu", index);
}
#define expect_bind_error(...) expect_bind_error_at(__FILE__, __LINE__, __VA_ARGS__)

static void test_each_kind(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    size_t idle = 0, b_walk = 0, left = 0, fly = 0;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_SPRITE, "idle", &idle, 0},
        {SPRAT_BIND_SPRITE_PATH, "b/walk.png", &b_walk, 0},
        {SPRAT_BIND_ANIMATION, "run-left", &left, 0},
        {SPRAT_BIND_ANIMATION, "fly", &fly, 0},
    };
    sprat_error error;
    memset(&error, 'x', sizeof(error));
    CHECK_INT(sprat_sheet_bind(sheet, bindings, 4, &error), SPRAT_OK);
    CHECK_INT(error.status, SPRAT_OK);
    CHECK_STR(error.message, "");
    CHECK_SIZE(idle, 2);
    CHECK_SIZE(b_walk, 1);
    CHECK_SIZE(left, 1);
    CHECK_SIZE(fly, 2);
    /* The indexes are the sheet's. */
    CHECK_STR(sheet->sprites[idle].path, "idle.png");
    CHECK_STR(sheet->animations[left].alias_of, "run");
    /* The error is optional. */
    idle = 0;
    CHECK_INT(sprat_sheet_bind(sheet, bindings, 1, NULL), SPRAT_OK);
    CHECK_SIZE(idle, 2);
    sprat_sheet_free(sheet);
}

static void test_optional(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    size_t coin = 0, idle = 0, swim = 0;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_SPRITE, "coin", &coin, SPRAT_BIND_OPTIONAL},
        {SPRAT_BIND_SPRITE, "idle", &idle, SPRAT_BIND_OPTIONAL},
        {SPRAT_BIND_ANIMATION, "swim", &swim, SPRAT_BIND_OPTIONAL},
    };
    CHECK_INT(sprat_sheet_bind(sheet, bindings, 3, NULL), SPRAT_OK);
    CHECK_SIZE(coin, SPRAT_NO_INDEX);
    CHECK_SIZE(idle, 2);
    CHECK_SIZE(swim, SPRAT_NO_INDEX);
    /* Optional does not excuse an ambiguous name. */
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "walk", SPRAT_BIND_OPTIONAL, SPRAT_ERR_AMBIGUOUS,
                      "bindings[0]: 2 sprites are named \"walk\": sprites[0] \"a/walk.png\" and "
                      "sprites[1] \"b/walk.png\"; bind one by its path with SPRAT_BIND_SPRITE_PATH");
    sprat_sheet_free(sheet);
}

/* One failing binding writes none of the indexes, even the ones that resolve. */
static void test_all_or_nothing(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    size_t idle = 777, swim = 777, coin = 777;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_SPRITE, "idle", &idle, 0},
        {SPRAT_BIND_SPRITE, "coin", &coin, SPRAT_BIND_OPTIONAL},
        {SPRAT_BIND_ANIMATION, "swim", &swim, 0},
    };
    sprat_error error;
    CHECK_INT(sprat_sheet_bind(sheet, bindings, 3, &error), SPRAT_ERR_NOT_FOUND);
    CHECK_SIZE(idle, 777);
    CHECK_SIZE(coin, 777);
    CHECK_SIZE(swim, 777);
    sprat_sheet_free(sheet);
}

static void test_not_found(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    /* Without a better hint, the message lists what the sheet has. */
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "jum", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"jum\" (the sheet has 4 sprites: \"walk\", "
                      "\"walk\", \"idle\", \"jump\")");
    expect_bind_error(sheet, SPRAT_BIND_SPRITE_PATH, "c/walk.png", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite has the path \"c/walk.png\" (the sheet has 4 sprites: "
                      "\"a/walk.png\", \"b/walk.png\", \"idle.png\", \"x/jump.png\")");
    expect_bind_error(sheet, SPRAT_BIND_ANIMATION, "swim", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no animation is named \"swim\" (the sheet has 3 animations: "
                      "\"run\", \"run-left\", \"fly\")");
    /* Names are compared byte for byte. */
    expect_bind_error(sheet, SPRAT_BIND_ANIMATION, "Run", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no animation is named \"Run\" (the sheet has 3 animations: "
                      "\"run\", \"run-left\", \"fly\")");
    sprat_sheet_free(sheet);
}

/* A name passed as the wrong kind is the likeliest mistake; say so. */
static void test_hints(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "idle.png", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"idle.png\", but a sprite has that path: "
                      "bind it with SPRAT_BIND_SPRITE_PATH");
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "fly", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"fly\", but an animation is: bind it with "
                      "SPRAT_BIND_ANIMATION");
    expect_bind_error(sheet, SPRAT_BIND_SPRITE_PATH, "idle", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite has the path \"idle\", but a sprite has that name: "
                      "bind it with SPRAT_BIND_SPRITE");
    expect_bind_error(sheet, SPRAT_BIND_ANIMATION, "jump", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no animation is named \"jump\", but a sprite is: bind it with "
                      "SPRAT_BIND_SPRITE");
    sprat_sheet_free(sheet);
}

static void test_ambiguous(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "walk", 0, SPRAT_ERR_AMBIGUOUS,
                      "bindings[0]: 2 sprites are named \"walk\": sprites[0] \"a/walk.png\" and "
                      "sprites[1] \"b/walk.png\"; bind one by its path with SPRAT_BIND_SPRITE_PATH");
    /* The path tells them apart. */
    size_t a = 0;
    sprat_binding b = {SPRAT_BIND_SPRITE_PATH, "a/walk.png", &a, 0};
    CHECK_INT(sprat_sheet_bind(sheet, &b, 1, NULL), SPRAT_OK);
    CHECK_SIZE(a, 0);
    sprat_sheet_free(sheet);

    /* Three of a name, and repeated animations. Paths are unique, so binding by
     * path is never ambiguous. */
    sheet = load_ok("sprat-raw 1\nsheet 1 0 1 0 2 4 3 ~ 0: 0:\natlas 8 8 0: 0: 0:\n"
                    "atlas 8 8 0: 0: 0:\n"
                    "sprite 0 0 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 3:a/s 0:\n"
                    "sprite 0 1 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 3:b/s 0:\n"
                    "sprite 1 0 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 3:c/s 0:\n"
                    "sprite 1 1 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:t 1:t 0:\n"
                    "anim 8 0 -1 0 3:run\nanim 8 0 -1 0 3:hop\nanim 8 0 -1 0 3:run\n");
    if (sheet == NULL) return;
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "s", 0, SPRAT_ERR_AMBIGUOUS,
                      "bindings[0]: 3 sprites are named \"s\", the first two: sprites[0] \"a/s\" and "
                      "sprites[1] \"b/s\"; bind one by its path with SPRAT_BIND_SPRITE_PATH");
    expect_bind_error(sheet, SPRAT_BIND_ANIMATION, "run", 0, SPRAT_ERR_AMBIGUOUS,
                      "bindings[0]: 2 animations are named \"run\": animations[0] and animations[2]");
    size_t c = 0;
    b.name = "c/s";
    b.index = &c;
    CHECK_INT(sprat_sheet_bind(sheet, &b, 1, NULL), SPRAT_OK);
    CHECK_SIZE(c, 2);
    sprat_sheet_free(sheet);
}

/* The first failure is described, and the count says how many more to fix. */
static void test_several_failures(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    size_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_SPRITE, "idle", &i0, 0},
        {SPRAT_BIND_SPRITE, "walk", &i1, 0},
        {SPRAT_BIND_ANIMATION, "swim", &i2, 0},
        {SPRAT_BIND_SPRITE_PATH, "nope", &i3, 0},
    };
    sprat_error error;
    sprat_status status = sprat_sheet_bind(sheet, bindings, 4, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_AMBIGUOUS, 0, 0,
                   "bindings[1], the first of 3 that fail: 2 sprites are named \"walk\": sprites[0] "
                   "\"a/walk.png\" and sprites[1] \"b/walk.png\"; bind one by its path with "
                   "SPRAT_BIND_SPRITE_PATH");
    sprat_sheet_free(sheet);
}

/* A sheet without sprites or animations, and long lists and names, which are
 * cut to fit the message. */
static void test_lists_and_long_names(void) {
    sprat_sheet* sheet = load_ok(HEADER(0, 0));
    if (sheet == NULL) return;
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "a", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"a\" (the sheet has no sprites)");
    expect_bind_error(sheet, SPRAT_BIND_ANIMATION, "a", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no animation is named \"a\" (the sheet has no animations)");
    sprat_sheet_free(sheet);

    char text[8192];
    size_t len = (size_t)snprintf(text, sizeof(text), HEADER_FMT, 40, 0, 0, 40 * 14);
    for (int i = 0; i < 40; ++i) {
        len += (size_t)snprintf(text + len, sizeof(text) - len,
                                "sprite 0 0 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 9:sprite_%02d "
                                "5:p%02d.x 0:\n", i, i);
    }
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, len, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) {
        fprintf(stderr, "  %s\n", error.message);
        return;
    }
    expect_bind_error(sheet, SPRAT_BIND_SPRITE, "sprite_99", 0, SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"sprite_99\" (the sheet has 40 sprites: "
                      "\"sprite_00\", \"sprite_01\", \"sprite_02\", \"sprite_03\", \"sprite_04\", "
                      "\"sprite_05\", \"sprite_06\", \"sprite_07\", ...)");
    expect_bind_error(sheet, SPRAT_BIND_SPRITE,
                      "a name far longer than anything a message should quote in full", 0,
                      SPRAT_ERR_NOT_FOUND,
                      "bindings[0]: no sprite is named \"a name far longer than anything a message "
                      "...\" (the sheet has 40 sprites: \"sprite_00\", \"sprite_01\", "
                      "\"sprite_02\", \"sprite_03\", \"sprite_04\", \"sprite_05\", ...)");
    sprat_sheet_free(sheet);
}

static void test_invalid_arguments(void) {
    sprat_sheet* sheet = load_ok(k_text);
    if (sheet == NULL) return;
    size_t index = 777;
    sprat_error error;
    sprat_binding b = {SPRAT_BIND_SPRITE, "idle", &index, 0};

    sprat_status status = sprat_sheet_bind(NULL, &b, 1, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "sheet must not be NULL");
    status = sprat_sheet_bind(sheet, NULL, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings is NULL but count is 2");
    /* No bindings is nothing to do. */
    CHECK_INT(sprat_sheet_bind(sheet, NULL, 0, &error), SPRAT_OK);
    CHECK_INT(sprat_sheet_bind(sheet, &b, 0, &error), SPRAT_OK);

    sprat_binding bad[2] = {b, b};
    bad[1].name = NULL;
    status = sprat_sheet_bind(sheet, bad, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings[1].name must not be NULL");
    bad[1] = b;
    bad[1].index = NULL;
    status = sprat_sheet_bind(sheet, bad, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings[1].index must not be NULL");
    bad[1] = b;
    bad[1].kind = (sprat_bind_kind)7;
    status = sprat_sheet_bind(sheet, bad, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings[1].kind is 7, which is not a sprat_bind_kind");
    bad[1].kind = (sprat_bind_kind)-1;
    status = sprat_sheet_bind(sheet, bad, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings[1].kind is -1, which is not a sprat_bind_kind");
    bad[1] = b;
    bad[1].flags = SPRAT_BIND_OPTIONAL | 0x30u;
    status = sprat_sheet_bind(sheet, bad, 2, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "bindings[1].flags has unknown bits 0x30");
    /* Arguments are checked before names: nothing was written. */
    CHECK_SIZE(index, 777);
    sprat_sheet_free(sheet);
}

/* A message whose list nearly fills the buffer still ends with its ")". */
static void test_list_always_closes(void) {
    char text[16384];
    size_t len = (size_t)snprintf(text, sizeof(text), HEADER_FMT, 60, 0, 0, 60 * 14);
    for (int i = 0; i < 60; ++i) {
        len += (size_t)snprintf(text + len, sizeof(text) - len,
                                "sprite 0 0 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 9:sprite_%02d "
                                "5:p%02d.x 0:\n", i, i);
    }
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, len, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) return;
    /* Names of every length shift where the list must stop. */
    char name[80];
    for (size_t n = 1; n < sizeof(name); ++n) {
        memset(name, 'n', n);
        name[n] = '\0';
        size_t index = 0;
        sprat_binding b = {SPRAT_BIND_SPRITE, name, &index, 0};
        CHECK_INT(sprat_sheet_bind(sheet, &b, 1, &error), SPRAT_ERR_NOT_FOUND);
        size_t m = strlen(error.message);
        if (m == 0 || error.message[m - 1] != ')') {
            FAIL_AT("name of %zu bytes: message does not close its list: \"%s\"", n, error.message);
        }
    }
    sprat_sheet_free(sheet);
}

/* Binding many names against many sprites sorts once instead of scanning per
 * name: 50000 x 50000 took minutes before. CTest's timeout catches a regression. */
static void test_many_bindings(void) {
    enum { COUNT = 50000 };
    size_t capacity = 256 + (size_t)COUNT * 80;
    char* buffer = malloc(capacity);
    sprat_binding* bindings = malloc(COUNT * sizeof(*bindings));
    size_t* indexes = malloc(COUNT * sizeof(*indexes));
    char (*names)[16] = malloc(COUNT * sizeof(*names));
    if (buffer == NULL || bindings == NULL || indexes == NULL || names == NULL) abort();
    size_t len = 256;
    for (int i = 0; i < COUNT; ++i) {
        len += (size_t)snprintf(buffer + len, capacity - len,
                                "sprite 0 0 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 7:s%06d 7:p%06d 0:\n",
                                i, i);
    }
    char header[256];
    int header_len = snprintf(header, sizeof(header), HEADER_FMT, COUNT, 0, 0, COUNT * 14);
    char* text = buffer + 256 - header_len;
    memcpy(text, header, (size_t)header_len);
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, (size_t)(buffer + len - text), &sheet, &error), SPRAT_OK);
    if (sheet == NULL) abort();
    /* Bind them in reverse, half by name and half by path. */
    for (int i = 0; i < COUNT; ++i) {
        int sprite = COUNT - 1 - i;
        snprintf(names[i], sizeof(names[i]), "%c%06d", i % 2 ? 'p' : 's', sprite);
        bindings[i].kind = i % 2 ? SPRAT_BIND_SPRITE_PATH : SPRAT_BIND_SPRITE;
        bindings[i].name = names[i];
        bindings[i].index = &indexes[i];
        bindings[i].flags = 0;
    }
    use_counting(0);
    CHECK_INT(sprat_sheet_bind(sheet, bindings, COUNT, &error), SPRAT_OK);
    CHECK_INT(g_counting.allocations, 1); /* one temporary index, freed */
    CHECK_INT(g_counting.live, 0);
    int wrong = 0;
    for (int i = 0; i < COUNT; ++i) wrong += indexes[i] != (size_t)(COUNT - 1 - i);
    CHECK_INT(wrong, 0);

    /* That allocation failing is reported, and writes nothing. */
    use_counting(1);
    indexes[0] = 777;
    sprat_status status = sprat_sheet_bind(sheet, bindings, COUNT, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_NOMEM, 0, 0, "out of memory");
    CHECK_SIZE(indexes[0], 777);
    use_c_library();
    sprat_sheet_free(sheet);
    free(names);
    free(indexes);
    free(bindings);
    free(buffer);
}

/* The intended use: bind, convert in sheet order, free, then use the indexes
 * with the converted data. */
typedef struct game_frame {
    int w;
    int h;
} game_frame;

static void test_indexes_outlive_the_sheet(void) {
    sprat_sheet* sheet = NULL;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    if (sheet == NULL) return;
    size_t run_02 = 0, run = 0, left = 0;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_SPRITE_PATH, "hero/run_02.png", &run_02, 0},
        {SPRAT_BIND_ANIMATION, "run", &run, 0},
        {SPRAT_BIND_ANIMATION, "run-left", &left, 0},
    };
    sprat_error error;
    CHECK_INT(sprat_sheet_bind(sheet, bindings, 3, &error), SPRAT_OK);

    game_frame frames[4];
    size_t run_frames[3];
    size_t frame_count = sheet->sprite_count < 4 ? sheet->sprite_count : 4;
    for (size_t i = 0; i < frame_count; ++i) {
        frames[i].w = sheet->sprites[i].rect.w;
        frames[i].h = sheet->sprites[i].rect.h;
    }
    size_t run_count = sheet->animations[run].frame_count;
    CHECK_SIZE(run_count, 3);
    if (run_count > 3) run_count = 3;
    memcpy(run_frames, sheet->animations[run].frames, run_count * sizeof(size_t));
    unsigned left_flip = sheet->animations[left].flip;
    sprat_sheet_free(sheet);

    CHECK_INT(frames[run_02].w, 32); /* stored turned: 32x16 */
    CHECK_INT(frames[run_02].h, 16);
    CHECK_SIZE(run_frames[1], run_02); /* frame indexes index the same array */
    CHECK_INT(left_flip, SPRAT_FLIP_H);
}

int main(void) {
    RUN(test_each_kind);
    RUN(test_optional);
    RUN(test_all_or_nothing);
    RUN(test_not_found);
    RUN(test_hints);
    RUN(test_ambiguous);
    RUN(test_several_failures);
    RUN(test_lists_and_long_names);
    RUN(test_invalid_arguments);
    RUN(test_list_always_closes);
    RUN(test_many_bindings);
    RUN(test_indexes_outlive_the_sheet);
    return TEST_RESULT();
}
