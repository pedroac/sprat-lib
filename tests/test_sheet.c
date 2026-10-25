/* Loading: what each record becomes in the model, the values at the edges of
 * what is accepted, aliases, the declared totals, memory, and scaling. Errors
 * have their own tests in test_errors.c, and files in test_file.c. */
#include "support.h"

/* ---- The fixture: real spratconvert output ----------------------------- */

static void test_fixture_sheet(void) {
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) {
        fprintf(stderr, "  %s\n", error.message);
        return;
    }
    CHECK_DOUBLE(sheet->scale, 0.5);
    CHECK_INT(sheet->extrude, 1);
    CHECK(sheet->multipack);
    CHECK_STR(sheet->normal_atlas_path, "n_0.png");
    CHECK_NULL(sheet->collision_atlas_path);
    CHECK_DOUBLE(sheet->collision_atlas_scale, 0);

    CHECK_SIZE(sheet->atlas_count, 2);
    CHECK_STR(sheet->atlases[0].path, "atlas_0.png");
    CHECK_STR(sheet->atlases[0].normal_path, "n_0.png");
    CHECK_NULL(sheet->atlases[0].collision_path);
    CHECK_INT(sheet->atlases[0].width, 64);
    CHECK_INT(sheet->atlases[0].height, 64);
    CHECK_STR(sheet->atlases[1].path, "atlas_1.png");
    CHECK_STR(sheet->atlases[1].normal_path, "n_1.png");
    CHECK_NULL(sheet->atlases[1].collision_path);
    CHECK_INT(sheet->atlases[1].width, 32);
    CHECK_INT(sheet->atlases[1].height, 32);
    sprat_sheet_free(sheet);
}

static void test_fixture_sprites(void) {
    sprat_sheet* sheet = NULL;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    if (sheet == NULL) return;
    CHECK_SIZE(sheet->sprite_count, 4);

    const sprat_sprite* run1 = &sheet->sprites[0];
    CHECK_STR(run1->name, "run_01");
    CHECK_STR(run1->path, "hero/run_01.png");
    CHECK_NULL(run1->alias_of);
    CHECK_SIZE(run1->atlas_index, 0);
    CHECK_INT(run1->rect.x, 0);
    CHECK_INT(run1->rect.y, 0);
    CHECK_INT(run1->rect.w, 16);
    CHECK_INT(run1->rect.h, 32);
    CHECK_INT(run1->rotation, 0);
    CHECK_INT(run1->trim.left, 1);
    CHECK_INT(run1->trim.top, 2);
    CHECK_INT(run1->trim.right, 3);
    CHECK_INT(run1->trim.bottom, 0);
    CHECK_INT(run1->pivot.x, 2);
    CHECK_INT(run1->pivot.y, 15);
    CHECK(!run1->has_slice);
    CHECK_SIZE(run1->marker_count, 2);
    const sprat_marker* pivot = &run1->markers[0];
    CHECK_STR(pivot->name, "pivot");
    CHECK_INT(pivot->type, SPRAT_MARKER_POINT);
    CHECK_INT(pivot->x, 2);
    CHECK_INT(pivot->y, 15);
    CHECK_NULL(pivot->vertices);
    CHECK_SIZE(pivot->vertex_count, 0);
    const sprat_marker* feet = &run1->markers[1];
    CHECK_STR(feet->name, "feet");
    CHECK_INT(feet->type, SPRAT_MARKER_RECTANGLE);
    CHECK_INT(feet->x, 0);
    CHECK_INT(feet->y, 14);
    CHECK_INT(feet->w, 8);
    CHECK_INT(feet->h, 2);

    /* Packed turned counter-clockwise; no pivot marker, so the global pivot. */
    const sprat_sprite* run2 = &sheet->sprites[1];
    CHECK_STR(run2->path, "hero/run_02.png");
    CHECK_INT(run2->rotation, 270);
    CHECK_INT(run2->rect.x, 16);
    CHECK_INT(run2->rect.w, 32);
    CHECK_INT(run2->rect.h, 16);
    CHECK_INT(run2->pivot.x, 4);
    CHECK_INT(run2->pivot.y, 8);
    CHECK_SIZE(run2->marker_count, 0);
    CHECK_NULL(run2->markers);

    const sprat_sprite* panel = &sheet->sprites[2];
    CHECK_STR(panel->name, "panel");
    CHECK_SIZE(panel->atlas_index, 1);
    CHECK(panel->has_slice);
    CHECK_INT(panel->slice.left, 2);
    CHECK_INT(panel->slice.top, 2);
    CHECK_INT(panel->slice.right, 2);
    CHECK_INT(panel->slice.bottom, 2);
    CHECK_INT(panel->slice.h_mode, SPRAT_SLICE_REPEAT);
    CHECK_INT(panel->slice.v_mode, SPRAT_SLICE_STRETCH);

    /* A deduplicated alias: the canonical region, its own trim. */
    const sprat_sprite* run3 = &sheet->sprites[3];
    CHECK_STR(run3->name, "run_03");
    CHECK_STR(run3->alias_of, "hero/run_01.png");
    CHECK(memcmp(&run3->rect, &run1->rect, sizeof(run1->rect)) == 0);
    CHECK_INT(run3->trim.left, 1);
    CHECK_INT(run3->trim.top, 1);
    CHECK_INT(run3->trim.right, 2);
    CHECK_INT(run3->trim.bottom, 2);
    sprat_sheet_free(sheet);
}

static void test_fixture_animations(void) {
    sprat_sheet* sheet = NULL;
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    if (sheet == NULL) return;
    CHECK_SIZE(sheet->animation_count, 2);
    const sprat_animation* run = &sheet->animations[0];
    CHECK_STR(run->name, "run");
    CHECK_INT(run->fps, 10);
    CHECK_INT(run->flip, 0);
    CHECK_NULL(run->alias_of);
    CHECK_SIZE(run->frame_count, 3);
    if (run->frame_count == 3) {
        CHECK_SIZE(run->frames[0], 0);
        CHECK_SIZE(run->frames[1], 1);
        CHECK_SIZE(run->frames[2], 3);
    }
    /* The alias replays its source at the source's fps, with its own flip. */
    const sprat_animation* left = &sheet->animations[1];
    CHECK_STR(left->name, "run-left");
    CHECK_STR(left->alias_of, "run");
    CHECK(left->alias_of == run->name);
    CHECK_INT(left->flip, SPRAT_FLIP_H);
    CHECK_INT(left->fps, 10);
    CHECK_SIZE(left->frame_count, 3);
    CHECK(left->frames == run->frames);
    sprat_sheet_free(sheet);
}

/* ---- sprat_sheet_load_memory() arguments and input --------------------- */

static void test_load_memory_arguments(void) {
    sprat_error error;
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_status status = sprat_sheet_load_memory("x", 1, NULL, &error);
    check_error_at(__FILE__, __LINE__, status, &error, NULL, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "out must not be NULL");
    status = sprat_sheet_load_memory(NULL, 3, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_INVALID_ARG, 0, 0,
                   "data is NULL but size is 3");
    /* NULL with size 0 is empty input, not a bad argument. */
    sheet = (sprat_sheet*)&sheet;
    status = sprat_sheet_load_memory(NULL, 0, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SYNTAX, 1, 1,
                   "not a sprat raw file: expected \"sprat-raw\", found the end of the file");
    /* The error is optional, on failure and on success. */
    CHECK_INT(sprat_sheet_load_memory(NULL, 3, &sheet, NULL), SPRAT_ERR_INVALID_ARG);
    CHECK_INT(sprat_sheet_load_memory("x", 1, &sheet, NULL), SPRAT_ERR_SYNTAX);
    CHECK_NULL(sheet);
    const char* text = with_totals(HEADER(0, 0));
    CHECK_INT(sprat_sheet_load_memory(text, strlen(text), &sheet, NULL), SPRAT_OK);
    sprat_sheet_free(sheet);
    /* A successful load clears the error. */
    memset(&error, 'x', sizeof(error));
    CHECK_INT(sprat_sheet_load_memory(text, strlen(text), &sheet, &error), SPRAT_OK);
    CHECK_INT(error.status, SPRAT_OK);
    CHECK_SIZE(error.line, 0);
    CHECK_STR(error.message, "");
    sprat_sheet_free(sheet);
}

/* Only `size` bytes are read, the input is not modified, and the sheet keeps
 * no pointer into it. */
static void test_input_is_borrowed(void) {
    size_t size;
    char* text = read_fixture(&size);
    char* copy = malloc(size + 8);
    if (copy == NULL) abort();
    memcpy(copy, text, size);
    memcpy(copy + size, "garbage", 8); /* past size, and no NUL right after the data */
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(copy, size, &sheet, &error), SPRAT_OK);
    CHECK(memcmp(copy, text, size) == 0);
    if (sheet != NULL) {
        const char* name = sheet->sprites[0].name;
        CHECK(name < copy || name >= copy + size + 8);
        memset(copy, 0, size + 8);
        CHECK_STR(sheet->sprites[0].name, "run_01");
        CHECK_STR(sheet->atlases[1].path, "atlas_1.png");
    }
    sprat_sheet_free(sheet);
    free(copy);
    free(text);
}

/* Any run of spaces, tabs, CR and LF separates tokens; the final newline is optional. */
static void test_whitespace(void) {
    sprat_sheet* sheet = load_ok("\r\n  sprat-raw\t1\r\nsheet  1 0 0 0\t1 1 0 ~ 0: 0:\r\n"
                                 "atlas 8 8 0: 0: 0:\n\n\n"
                                 "sprite\t0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:b 0:");
    if (sheet != NULL) {
        CHECK_SIZE(sheet->sprite_count, 1);
        CHECK_STR(sheet->sprites[0].path, "b");
    }
    sprat_sheet_free(sheet);
    sheet = load_ok(HEADER(0, 0) "   \n\t\r\n");
    sprat_sheet_free(sheet);
}

static void test_minimal_sheet(void) {
    sprat_sheet* sheet = load_ok(HEADER(0, 0));
    if (sheet == NULL) return;
    CHECK_DOUBLE(sheet->scale, 1.0);
    CHECK_INT(sheet->extrude, 0);
    CHECK(!sheet->multipack);
    CHECK_SIZE(sheet->atlas_count, 1);
    CHECK_STR(sheet->atlases[0].path, ""); /* kept as written; check paths before opening */
    CHECK_NULL(sheet->atlases[0].normal_path);
    CHECK_NULL(sheet->atlases[0].collision_path);
    CHECK_SIZE(sheet->sprite_count, 0);
    CHECK_NULL(sheet->sprites);
    CHECK_SIZE(sheet->animation_count, 0);
    CHECK_NULL(sheet->animations);
    CHECK_NULL(sheet->normal_atlas_path);
    CHECK_NULL(sheet->collision_atlas_path);
    sprat_sheet_free(sheet);
}

/* ---- Records ------------------------------------------------------------ */

static void test_sheet_line(void) {
    sprat_sheet* sheet = load_ok("sprat-raw 1\nsheet 0.25 16777216 1 2.5e-1 2 0 0 ~ 3:n.p 3:c.p\n"
                                 "atlas 8 8 1:a 3:a.n 3:a.c\natlas 16777216 1 1:b 0: 0:\n");
    if (sheet == NULL) return;
    CHECK_DOUBLE(sheet->scale, 0.25);
    CHECK_INT(sheet->extrude, SPRAT_COORD_LIMIT);
    CHECK(sheet->multipack);
    CHECK_DOUBLE(sheet->collision_atlas_scale, 0.25);
    CHECK_STR(sheet->normal_atlas_path, "n.p");
    CHECK_STR(sheet->collision_atlas_path, "c.p");
    CHECK_SIZE(sheet->atlas_count, 2);
    CHECK_STR(sheet->atlases[0].normal_path, "a.n");
    CHECK_STR(sheet->atlases[0].collision_path, "a.c");
    CHECK_INT(sheet->atlases[1].width, SPRAT_COORD_LIMIT);
    CHECK_INT(sheet->atlases[1].height, 1);
    sprat_sheet_free(sheet);
}

/* Lengths count UTF-8 bytes; strings may hold spaces, colons, newlines and
 * every valid code point. */
static void test_strings(void) {
    sprat_sheet* sheet = load_ok(HEADER(1, 0) SPRITE_AT(
        "0 0 4 4", "0 8:a b:\nc\xC3\xA9 12:\xE2\x9C\x93/\xF0\x9F\x90\x9F.png 0:"));
    if (sheet != NULL) {
        CHECK_STR(sheet->sprites[0].name, "a b:\nc\xC3\xA9");
        CHECK_STR(sheet->sprites[0].path, "\xE2\x9C\x93/\xF0\x9F\x90\x9F.png");
    }
    sprat_sheet_free(sheet);
}

/* The largest values the loader accepts, and the smallest. */
static void test_sprite_limits(void) {
    sprat_sheet* sheet = load_ok(
        "sprat-raw 1\nsheet 1 0 0 0 1 2 0 ~ 0: 0:\natlas 16777216 8 0: 0: 0:\n"
        /* A rect touching the atlas' far corner, the largest trim the source
         * size allows, and pivots at both ends of the coordinate range. */
        "sprite 0 16777212 4 4 4 0 16777212 0 0 0 -16777216 16777216 0 0 0 0 0 0 0 0 1:a 1:a 0:\n"
        /* An empty rect at the edge of the atlas. */
        "sprite 0 16777216 8 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:b 1:b 0:\n");
    if (sheet == NULL) return;
    const sprat_sprite* a = &sheet->sprites[0];
    CHECK_INT(a->rect.x, 16777212);
    CHECK_INT(a->trim.left, 16777212);
    CHECK_INT(sprat_sprite_source_size(a).w, SPRAT_COORD_LIMIT);
    CHECK_INT(a->pivot.x, -SPRAT_COORD_LIMIT);
    CHECK_INT(a->pivot.y, SPRAT_COORD_LIMIT);
    const sprat_sprite* b = &sheet->sprites[1];
    CHECK_INT(b->rect.x, 16777216);
    CHECK_INT(b->rect.w, 0);
    CHECK_INT(sprat_sprite_content_size(b).w, 0);
    sprat_sheet_free(sheet);
}

static void test_slices(void) {
    sprat_sheet* sheet = load_ok(
        HEADER(3, 0)
        /* Insets that exactly fill a 4x4 source, with every fill mode. */
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 1 1 1 3 3 2 3 0 1:a 1:a 0:\n"
        /* Trim counts: a 2x2 rect with trim 1 1 1 1 is a 4x4 source. */
        "sprite 0 0 0 2 2 0 1 1 1 1 0 0 1 2 2 2 2 0 1 0 1:b 1:b 0:\n"
        /* has_slice 0: whatever the insets say, the slice is zeroed. */
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 1 2 1 2 3 3 0 1:c 1:c 0:\n");
    if (sheet == NULL) return;
    const sprat_slice* a = &sheet->sprites[0].slice;
    CHECK_INT(a->left + a->right, 4);
    CHECK_INT(a->top + a->bottom, 4);
    CHECK_INT(a->h_mode, SPRAT_SLICE_MIRROR);
    CHECK_INT(a->v_mode, SPRAT_SLICE_FIXED);
    const sprat_slice* b = &sheet->sprites[1].slice;
    CHECK_INT(b->left, 2);
    CHECK_INT(b->h_mode, SPRAT_SLICE_STRETCH);
    CHECK_INT(b->v_mode, SPRAT_SLICE_REPEAT);
    const sprat_sprite* c = &sheet->sprites[2];
    CHECK(!c->has_slice);
    CHECK(c->slice.left == 0 && c->slice.top == 0 && c->slice.right == 0 && c->slice.bottom == 0);
    CHECK(c->slice.h_mode == SPRAT_SLICE_STRETCH && c->slice.v_mode == SPRAT_SLICE_STRETCH);
    sprat_sheet_free(sheet);
}

/* Each marker type, and markers and vertices spread over several sprites. */
static void test_markers(void) {
    sprat_sheet* sheet = load_ok(
        HEADER(3, 0)
        SPRITE_AT("0 0 8 8", "3 1:a 1:a 0:")
        "marker 0 -5 6 0 0 0 0 1:p\n"
        "marker 1 1 2 3 0 0 0 1:c\n"
        "marker 2 1 2 0 4 5 0 1:r\n"
        SPRITE_AT("0 0 8 8", "0 1:b 1:b 0:")
        SPRITE_AT("0 0 8 8", "2 1:c 1:c 0:")
        /* spratconvert writes 0,0 for a polygon's x, y; the model holds its first vertex. */
        "marker 3 0 0 0 0 0 3 1 2 3 4 -5 -6 1:q\n"
        "marker 3 0 0 0 0 0 1 7 8 0:\n");
    if (sheet == NULL) return;
    const sprat_sprite* a = &sheet->sprites[0];
    CHECK_SIZE(a->marker_count, 3);
    CHECK(a->markers[0].type == SPRAT_MARKER_POINT && a->markers[0].x == -5 && a->markers[0].y == 6);
    CHECK(a->markers[1].type == SPRAT_MARKER_CIRCLE && a->markers[1].radius == 3);
    CHECK(a->markers[2].type == SPRAT_MARKER_RECTANGLE && a->markers[2].w == 4
          && a->markers[2].h == 5);
    CHECK_SIZE(sheet->sprites[1].marker_count, 0);
    CHECK_NULL(sheet->sprites[1].markers);

    const sprat_sprite* c = &sheet->sprites[2];
    CHECK_SIZE(c->marker_count, 2);
    CHECK(c->markers == a->markers + 3); /* one section, sliced in file order */
    const sprat_marker* q = &c->markers[0];
    CHECK_STR(q->name, "q");
    CHECK_INT(q->type, SPRAT_MARKER_POLYGON);
    CHECK_SIZE(q->vertex_count, 3);
    CHECK_INT(q->x, 1);
    CHECK_INT(q->y, 2);
    CHECK_INT(q->vertices[1].x, 3);
    CHECK_INT(q->vertices[2].y, -6);
    const sprat_marker* single = &c->markers[1];
    CHECK_STR(single->name, "");
    CHECK_SIZE(single->vertex_count, 1);
    CHECK(single->vertices == q->vertices + 3);
    CHECK(single->x == 7 && single->y == 8);
    sprat_sheet_free(sheet);
}

static void test_animations(void) {
    sprat_sheet* sheet = load_ok(
        HEADER(2, 6) SPRITE SPRITE_AT("4 0 4 4", "0 1:b 5:b.png 0:")
        "anim 100 3 -1 4 1 0 1 1 4:walk\n"
        /* A regular animation may have no frames. */
        "anim 5 0 -1 0 5:empty\n"
        /* Aliases of aliases resolve to the regular animation at the end. */
        "anim 9 2 3 0 2:a1\n"
        "anim 9 1 0 0 2:a2\n"
        /* An alias that comes before its source. */
        "anim 9 0 5 0 2:a3\n"
        "anim 30 0 -1 1 1 4:last\n");
    if (sheet == NULL) return;
    const sprat_animation* walk = &sheet->animations[0];
    CHECK_INT(walk->fps, SPRAT_FPS_LIMIT); /* the fastest accepted */
    CHECK_INT(walk->flip, SPRAT_FLIP_H | SPRAT_FLIP_V);
    CHECK_SIZE(walk->frame_count, 4);
    CHECK(walk->frames[0] == 1 && walk->frames[1] == 0 && walk->frames[3] == 1);
    const sprat_animation* empty = &sheet->animations[1];
    CHECK_SIZE(empty->frame_count, 0);
    CHECK_NULL(empty->frames);
    CHECK_NULL(empty->alias_of);

    /* Flips combine along the chain: walk is H|V; a2 (H) of walk is V; a1 (V)
     * of a2 is unflipped. */
    const sprat_animation* a1 = &sheet->animations[2];
    CHECK_STR(a1->alias_of, "a2"); /* the animation it names, not the end of the chain */
    CHECK_INT(a1->fps, walk->fps);
    CHECK(a1->frames == walk->frames && a1->frame_count == 4);
    CHECK_INT(a1->flip, 0);
    const sprat_animation* a2 = &sheet->animations[3];
    CHECK_STR(a2->alias_of, "walk");
    CHECK_INT(a2->flip, SPRAT_FLIP_V);
    const sprat_animation* a3 = &sheet->animations[4];
    CHECK_STR(a3->alias_of, "last");
    CHECK_INT(a3->fps, 30);
    CHECK_SIZE(a3->frame_count, 1);
    CHECK_SIZE(a3->frames[0], 1);
    sprat_sheet_free(sheet);
}

/* ---- Sprite aliases ----------------------------------------------------- */

static void test_sprite_aliases(void) {
    /* An alias shares its canonical sprite's atlas, region and rotation, keeps
     * its own trim, and may come before or after it in the file. */
    sprat_sheet* sheet = load_ok(
        "sprat-raw 1\nsheet 1 0 1 0 2 4 0 ~ 0: 0:\natlas 8 8 0: 0: 0:\natlas 8 8 0: 0: 0:\n"
        "sprite 1 4 4 4 4 90 1 1 1 1 0 0 0 0 0 0 0 0 0 0 1:b 5:b.png 5:d.png\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 5:a.png 0:\n"
        "sprite 1 4 4 4 4 90 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:d 5:d.png 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:c 5:c.png 5:a.png\n");
    if (sheet == NULL) return;
    CHECK_STR(sheet->sprites[0].alias_of, "d.png");
    CHECK_INT(sheet->sprites[0].trim.left, 1);
    CHECK_STR(sheet->sprites[3].alias_of, "a.png");
    sprat_sheet_free(sheet);
}

/* More than 256 regular sprites moves the alias check's arrays to the heap:
 * one more allocation, freed before the load returns, and still correct. */
static void test_sprite_aliases_on_the_heap(void) {
    enum { REGULAR = 300 };
    size_t capacity = 256 + (REGULAR + 1) * 80;
    char* buffer = malloc(capacity);
    if (buffer == NULL) abort();
    size_t string_bytes = 0;
    size_t len = 256; /* room for the header, written in front once the totals are known */
    /* Regular sprites in reverse path order, so sorting moves every one. */
    for (int i = REGULAR - 1; i >= 0; --i) {
        char path[16];
        int n = snprintf(path, sizeof(path), "p%03d", i);
        string_bytes += 1 + (size_t)n;
        len += (size_t)snprintf(buffer + len, capacity - len,
                                "sprite 0 %d 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s %d:%s 0:\n",
                                i % 8, n, path);
    }
    string_bytes += 1 + 4 + 4;
    char* alias = buffer + len;
    len += (size_t)snprintf(buffer + len, capacity - len,
                            "sprite 0 3 0 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 4:a.pn 4:p003\n");
    char header[256];
    int header_len = snprintf(header, sizeof(header), HEADER_FMT, REGULAR + 1, 0, 0,
                              (int)string_bytes);
    char* text = buffer + 256 - header_len;
    memcpy(text, header, (size_t)header_len);
    size_t size = (size_t)(buffer + len - text);

    use_counting(0);
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, size, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) fprintf(stderr, "  %s\n", error.message);
    CHECK_INT(g_counting.allocations, 2);
    CHECK_INT(g_counting.live, sheet != NULL ? 1 : 0);
    sprat_sheet_free(sheet);

    /* Fail each allocation in turn, the alias check's included. */
    for (long n = 1; n <= 2; ++n) {
        use_counting(n);
        sheet = (sprat_sheet*)&sheet;
        sprat_status status = sprat_sheet_load_memory(text, size, &sheet, &error);
        check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_NOMEM, 0, 0,
                       "out of memory");
        CHECK_INT(g_counting.live, 0);
    }

    /* The same sheet with the alias moved off its canonical sprite's region. */
    alias[strlen("sprite 0 ")] = '4';
    use_counting(0);
    sheet = (sprat_sheet*)&sheet;
    sprat_status status = sprat_sheet_load_memory(text, size, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SCHEMA, 0, 0,
                   "sprites[300].alias_of: \"p003\" is in atlas 0 at 3,0 1x1 rotated 0, but this "
                   "alias is in atlas 0 at 4,0 1x1 rotated 0");
    CHECK_INT(g_counting.live, 0);
    use_c_library();
    free(buffer);
}

/* ---- Declared totals and memory ----------------------------------------- */

/* Huge totals in a short file are refused before anything is allocated. */
static void test_totals_are_checked_first(void) {
    use_counting(0);
    expect_error("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 2000000 0: 0:\natlas 8 8 0: 0: 0:\n",
                 SPRAT_ERR_SCHEMA, 2, 1,
                 "sheet: the file is too short for the records it declares (they need at least "
                 "2000020 more bytes, 26 are left)");
    expect_error("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 2147483647 0: 0:\natlas 8 8 0: 0: 0:\n",
                 SPRAT_ERR_SCHEMA, 2, 1,
                 "sheet: the file is too short for the records it declares (they need at least "
                 "2147483667 more bytes, 26 are left)");
    CHECK_INT(g_counting.allocations, 0);
    use_c_library();
}

/* A sheet is one allocation: its block. */
static void test_one_allocation(void) {
    size_t size;
    char* text = read_fixture(&size);
    use_counting(0);
    sprat_sheet* sheet = NULL;
    CHECK_INT(sprat_sheet_load_memory(text, size, &sheet, NULL), SPRAT_OK);
    CHECK_INT(g_counting.allocations, 1); /* aliases checked on the stack */
    CHECK_INT(g_counting.live, 1);
    sprat_sheet_free(sheet);
    CHECK_INT(g_counting.live, 0);

    /* Loading a file adds the read buffer, freed before the load returns. */
    use_counting(0);
    CHECK_INT(sprat_sheet_load_file(MULTIPACK_FIXTURE, &sheet, NULL), SPRAT_OK);
    CHECK_INT(g_counting.allocations, 2 + PATH_ALLOCATIONS);
    CHECK_INT(g_counting.live, 1);
    sprat_sheet_free(sheet);
    CHECK_INT(g_counting.live, 0);
    use_c_library();
    free(text);
}

/* A failed allocation from load_memory reports NOMEM and leaks nothing. */
static void test_out_of_memory(void) {
    size_t size;
    char* text = read_fixture(&size);
    use_counting(1);
    sprat_sheet* sheet = (sprat_sheet*)&sheet;
    sprat_error error;
    sprat_status status = sprat_sheet_load_memory(text, size, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_NOMEM, 0, 0,
                   "out of memory");
    CHECK_INT(g_counting.live, 0);
    use_c_library();
    free(text);
}

/* ---- Scaling: hostile files must load in about linear time -------------- */

/* Each animation aliases the next, ending at a regular one. Resolving used to
 * walk every chain from its start, which is quadratic: this sheet took minutes.
 * CTest's timeout (tests/CMakeLists.txt) turns a regression into a failure. */
static void test_long_alias_chain(void) {
    enum { COUNT = 300000 };
    size_t capacity = (size_t)COUNT * 48 + 256;
    char* body = malloc(capacity);
    char* text = malloc(capacity + 256);
    if (body == NULL || text == NULL) abort();
    size_t body_len = (size_t)snprintf(body, capacity, "%s", SPRITE);
    size_t string_bytes = 6 + 4; /* "a", "a.png" and "last" */
    for (int i = 0; i < COUNT - 1; ++i) {
        char name[16];
        int name_len = snprintf(name, sizeof(name), "a%d", i);
        string_bytes += (size_t)name_len;
        body_len += (size_t)snprintf(body + body_len, capacity - body_len, "anim 8 0 %d 0 %d:%s\n",
                                     i + 1, name_len, name);
    }
    body_len += (size_t)snprintf(body + body_len, capacity - body_len, "anim 24 0 -1 2 0 0 4:last\n");
    size_t len = (size_t)snprintf(text, 256, HEADER_FMT, 1, COUNT, 2, (int)string_bytes);
    memcpy(text + len, body, body_len);
    len += body_len;
    free(body);

    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, len, &sheet, &error), SPRAT_OK);
    if (sheet != NULL) {
        const sprat_animation* first = &sheet->animations[0];
        CHECK_INT(first->fps, 24);
        CHECK_SIZE(first->frame_count, 2);
        CHECK_STR(first->alias_of, "a1");
        CHECK_NULL(sheet->animations[COUNT - 1].alias_of);
    }
    sprat_sheet_free(sheet);
    free(text);
}

/* Many aliases of one sprite, among many sprites, and many sprites with one
 * path. A hash table keyed by path took minutes on such sheets: every insert
 * and every lookup walked a run of equal or colliding paths. */
static void test_many_sprites_and_aliases(void) {
    enum { REGULAR = 100000, ALIASES = 100000 };
    size_t capacity = 256 + (size_t)(REGULAR + ALIASES) * 80;
    char* buffer = malloc(capacity);
    if (buffer == NULL) abort();
    size_t len = 256;
    size_t string_bytes = 0;
    for (int i = 0; i < REGULAR + ALIASES; ++i) {
        char path[16];
        int n = snprintf(path, sizeof(path), "%c%06d", i < REGULAR ? 'p' : 'q', i);
        bool alias = i >= REGULAR;
        string_bytes += 1 + (size_t)n + (alias ? 7 : 0);
        len += (size_t)snprintf(buffer + len, capacity - len,
                                "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s %d:%s %s\n", n,
                                path, alias ? "7:p000007" : "0:");
    }
    char header[256];
    int header_len = snprintf(header, sizeof(header), HEADER_FMT, REGULAR + ALIASES, 0, 0,
                              (int)string_bytes);
    char* text = buffer + 256 - header_len;
    memcpy(text, header, (size_t)header_len);
    size_t size = (size_t)(buffer + len - text);

    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, size, &sheet, &error), SPRAT_OK);
    if (sheet == NULL) fprintf(stderr, "  %s\n", error.message);
    if (sheet != NULL) CHECK_STR(sheet->sprites[REGULAR].alias_of, "p000007");
    sprat_sheet_free(sheet);
    free(buffer);

    /* Every sprite with one path: rejected, and quickly. */
    const char* same = SPRITE_AT("0 0 4 4", "0 1:a 5:a.png 0:");
    enum { SAME = 200000 };
    capacity = 256 + (size_t)SAME * strlen(same);
    text = malloc(capacity);
    if (text == NULL) abort();
    len = (size_t)snprintf(text, 256, HEADER_FMT, SAME, 0, 0, SAME * 6);
    for (int i = 0; i < SAME; ++i) {
        memcpy(text + len, same, strlen(same));
        len += strlen(same);
    }
    sheet = (sprat_sheet*)&sheet;
    sprat_status status = sprat_sheet_load_memory(text, len, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SCHEMA, 0, 0,
                   "sprites[1].path: \"a.png\" is also the path of sprites[0]; paths are unique");
    free(text);
}

int main(void) {
    RUN(test_fixture_sheet);
    RUN(test_fixture_sprites);
    RUN(test_fixture_animations);
    RUN(test_load_memory_arguments);
    RUN(test_input_is_borrowed);
    RUN(test_whitespace);
    RUN(test_minimal_sheet);
    RUN(test_sheet_line);
    RUN(test_strings);
    RUN(test_sprite_limits);
    RUN(test_slices);
    RUN(test_markers);
    RUN(test_animations);
    RUN(test_sprite_aliases);
    RUN(test_sprite_aliases_on_the_heap);
    RUN(test_totals_are_checked_first);
    RUN(test_one_allocation);
    RUN(test_out_of_memory);
    RUN(test_long_alias_chain);
    RUN(test_many_sprites_and_aliases);
    printf("sprat %s\n", sprat_version());
    return TEST_RESULT();
}
