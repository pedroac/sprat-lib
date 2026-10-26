/* Maps the records written by spratconvert's `raw` transform (transforms/raw.jsonnet
 * in sprat-cli) onto the public model. The input is read in place. The sheet line
 * declares how many records and string bytes follow, so the whole model goes into
 * one block of exactly the right size: one allocation per sheet, with each record
 * taking its slice of a section (markers, vertices, frames, strings, ...). */
#include "error.h"
#include "memory.h"
#include "reader.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef SPRAT_VERSION_STRING
#define SPRAT_VERSION_STRING "unknown"
#endif

/* Versions of the raw format this library reads. Version 2 added each animation's
 * LOOP; version 1 files load with loop 0, which is how they played. */
#define SPRAT_RAW_MIN_VERSION 1
#define SPRAT_RAW_VERSION 2

/* Largest file length sprat_sheet_load_file() sizes its buffer from up front
 * (64 MiB, far beyond any real sheet); longer files are read in growing chunks. */
#define SPRAT_TRUSTED_FILE_LENGTH (64L * 1024 * 1024)

/* The start of a sheet's block: the sheet, then the free function that releases
 * the block, so sprat_sheet_free() uses the functions it was loaded with. */
typedef struct sheet_storage {
    sprat_sheet sheet;
    sprat_free_func free_func;
} sheet_storage;

/* What the sheet line declares follows it. */
typedef struct totals {
    size_t atlases;
    size_t sprites;
    size_t animations;
    size_t markers;
    size_t vertices;
    size_t frames;
    size_t string_bytes;
} totals;

typedef struct loader {
    sprat_reader* in;
    sprat_error* error;
    const sprat_memory* memory;
    totals declared;
    int version;       /* of the raw format */
    size_t sheet_line; /* input offset of the sheet record, for errors about its totals */
    /* The unused rest of each section; records take their slices from the front.
     * Every one must be used up exactly by the end of the file. */
    sprat_marker* markers;
    size_t markers_left;
    sprat_point* vertices;
    size_t vertices_left;
    size_t* frames;
    size_t frames_left;
} loader;

#define NO_INDEX ((size_t)-1)
#define NO_POS ((size_t)-1)

/* Names the value being loaded after the public model, as in
 * sprites[3].markers[0].vertices[2].x. Each level points to the one that holds
 * it, and the name is formatted only when an error needs it, so successful
 * loads never pay for it. */
typedef struct where {
    const struct where* parent; /* NULL at the top */
    const char* name;
    size_t index; /* NO_INDEX when name is not a list */
} where;

static const where k_sheet = {NULL, "sheet", NO_INDEX};

static where at_index(const where* parent, const char* list, size_t index) {
    where w = {parent, list, index};
    return w;
}

static where at_field(const where* parent, const char* field) {
    where w = {parent, field, NO_INDEX};
    return w;
}

static size_t format_where(const where* w, char* out, size_t size) {
    size_t n = w->parent != NULL ? format_where(w->parent, out, size) : 0;
    if (n < size) {
        n += (size_t)snprintf(out + n, size - n, "%s%s", n > 0 ? "." : "", w->name);
    }
    if (w->index != NO_INDEX && n < size) {
        n += (size_t)snprintf(out + n, size - n, "[%zu]", w->index);
    }
    return n;
}

/* Fails with SPRAT_ERR_SCHEMA and "<where>: <message>", located at input
 * offset pos unless that is NO_POS. */
static bool schema_error(loader* l, const where* w, size_t pos, const char* fmt, ...) {
    char name[96];
    char what[160];
    format_where(w, name, sizeof(name));
    va_list args;
    va_start(args, fmt);
    vsnprintf(what, sizeof(what), fmt, args);
    va_end(args);
    sprat_set_error(l->error, SPRAT_ERR_SCHEMA, "%s: %s", name, what);
    if (pos != NO_POS) sprat_reader_locate(l->in, pos, &l->error->line, &l->error->column);
    return false;
}

/* Puts prefix in front of the error message, cutting the end if it no longer fits. */
static void prefix_message(sprat_error* error, const char* prefix) {
    const size_t size = sizeof(error->message);
    size_t n = strlen(prefix);
    if (n > size - 1) n = size - 1;
    size_t rest = strlen(error->message);
    if (rest > size - 1 - n) rest = size - 1 - n;
    memmove(error->message + n, error->message, rest);
    memcpy(error->message, prefix, n);
    error->message[n + rest] = '\0';
}

/* Prefixes the reader's error with the name of the value it was reading. */
static bool name_failure(loader* l, const where* w) {
    char name[100];
    size_t n = format_where(w, name, sizeof(name) - 2);
    if (n > sizeof(name) - 3) n = sizeof(name) - 3;
    memcpy(name + n, ": ", 3);
    prefix_message(l->error, name);
    return false;
}

static bool nomem(loader* l) {
    sprat_set_error(l->error, SPRAT_ERR_NOMEM, "out of memory");
    return false;
}

/* ---- Field readers ------------------------------------------------------ */

/* Each reads the value named `field` of `parent` and names it in any error. */

static bool read_tag(loader* l, const where* record, const char* tag) {
    return sprat_reader_tag(l->in, tag) || name_failure(l, record);
}

static bool read_int(loader* l, const where* parent, const char* field, int* out) {
    if (sprat_reader_int(l->in, out)) return true;
    where w = at_field(parent, field);
    return name_failure(l, &w);
}

static bool read_double(loader* l, const where* parent, const char* field, double* out) {
    if (sprat_reader_double(l->in, out)) return true;
    where w = at_field(parent, field);
    return name_failure(l, &w);
}

/* An integer in min..max. Coordinates, sizes and insets use the
 * SPRAT_COORD_LIMIT range, and say so when they leave it. */
static bool read_in_range(loader* l, const where* parent, const char* field, int min, int max,
                          int* out) {
    if (!read_int(l, parent, field, out)) return false;
    if (*out >= min && *out <= max) return true;
    where w = at_field(parent, field);
    size_t pos = l->in->token_start;
    if (min == 0 && *out < 0) return schema_error(l, &w, pos, "must not be negative, found %d", *out);
    if (min == 1 && *out < 1) return schema_error(l, &w, pos, "must be at least 1, found %d", *out);
    const char* limit = max == SPRAT_COORD_LIMIT ? " (SPRAT_COORD_LIMIT)"
                      : max == SPRAT_FPS_LIMIT ? " (SPRAT_FPS_LIMIT)"
                                               : "";
    return schema_error(l, &w, pos, "%d is out of range %d..%d%s", *out, min, max, limit);
}

/* A size, inset or margin. */
static bool read_size(loader* l, const where* parent, const char* field, int* out) {
    return read_in_range(l, parent, field, 0, SPRAT_COORD_LIMIT, out);
}

/* A position, which may be negative. */
static bool read_coord(loader* l, const where* parent, const char* field, int* out) {
    return read_in_range(l, parent, field, -SPRAT_COORD_LIMIT, SPRAT_COORD_LIMIT, out);
}

static bool read_bool(loader* l, const where* parent, const char* field, bool* out) {
    int value;
    if (!read_int(l, parent, field, &value)) return false;
    if (value != 0 && value != 1) {
        where w = at_field(parent, field);
        return schema_error(l, &w, l->in->token_start, "must be 0 or 1, found %d", value);
    }
    *out = value == 1;
    return true;
}

/* An enum stored as 0..count-1. */
static bool read_enum(loader* l, const where* parent, const char* field, int count, int* out) {
    if (!read_int(l, parent, field, out)) return false;
    if (*out < 0 || *out >= count) {
        where w = at_field(parent, field);
        return schema_error(l, &w, l->in->token_start, "unknown value %d (expected 0..%d)", *out,
                            count - 1);
    }
    return true;
}

static bool read_count(loader* l, const where* parent, const char* field, size_t* out) {
    if (sprat_reader_count(l->in, out)) return true;
    where w = at_field(parent, field);
    return name_failure(l, &w);
}

static bool read_string(loader* l, const where* parent, const char* field, const char** out) {
    if (sprat_reader_string(l->in, out)) return true;
    where w = at_field(parent, field);
    return name_failure(l, &w);
}

/* The raw format writes "" for "none"; the model uses NULL there. */
static bool read_optional_string(loader* l, const where* parent, const char* field,
                                 const char** out) {
    if (!read_string(l, parent, field, out)) return false;
    if ((*out)[0] == '\0') *out = NULL;
    return true;
}

/* ---- Records ------------------------------------------------------------ */

static bool load_atlas(loader* l, const where* at, sprat_atlas* a) {
    return read_tag(l, at, "atlas")
        && read_in_range(l, at, "width", 1, SPRAT_COORD_LIMIT, &a->width)
        && read_in_range(l, at, "height", 1, SPRAT_COORD_LIMIT, &a->height)
        && read_string(l, at, "path", &a->path)
        && read_optional_string(l, at, "normal_path", &a->normal_path)
        && read_optional_string(l, at, "collision_path", &a->collision_path);
}

static const char* marker_type_name(sprat_marker_type type) {
    switch (type) {
    case SPRAT_MARKER_POINT: return "point";
    case SPRAT_MARKER_CIRCLE: return "circle";
    case SPRAT_MARKER_RECTANGLE: return "rectangle";
    case SPRAT_MARKER_POLYGON: return "polygon";
    }
    return "?";
}

static bool load_marker(loader* l, const where* at, sprat_marker* m) {
    int type;
    size_t count;
    if (!read_tag(l, at, "marker")
        || !read_enum(l, at, "type", SPRAT_MARKER_POLYGON + 1, &type)
        || !read_coord(l, at, "x", &m->x)
        || !read_coord(l, at, "y", &m->y)
        || !read_size(l, at, "radius", &m->radius)
        || !read_size(l, at, "w", &m->w)
        || !read_size(l, at, "h", &m->h)
        || !read_count(l, at, "vertex_count", &count)) {
        return false;
    }
    m->type = (sprat_marker_type)type;
    size_t count_pos = l->in->token_start;
    where count_at = at_field(at, "vertex_count");
    if (count > 0 && m->type != SPRAT_MARKER_POLYGON) {
        return schema_error(l, &count_at, count_pos, "only polygons have vertices, this is a %s",
                            marker_type_name(m->type));
    }
    if (count == 0 && m->type == SPRAT_MARKER_POLYGON) {
        return schema_error(l, &count_at, count_pos, "a polygon needs at least one vertex");
    }
    if (count > 0) {
        if (count > l->vertices_left) {
            return schema_error(l, &count_at, count_pos,
                                "%zu is more than the %zu left of the sheet line's vertex_total (%zu)",
                                count, l->vertices_left, l->declared.vertices);
        }
        sprat_point* points = l->vertices;
        l->vertices += count;
        l->vertices_left -= count;
        for (size_t i = 0; i < count; ++i) {
            where vertex = at_index(at, "vertices", i);
            if (!read_coord(l, &vertex, "x", &points[i].x)
                || !read_coord(l, &vertex, "y", &points[i].y)) {
                return false;
            }
        }
        m->vertices = points;
        m->vertex_count = count;
        /* spratconvert writes 0,0 here; the model promises the first vertex. */
        m->x = points[0].x;
        m->y = points[0].y;
    }
    return read_string(l, at, "name", &m->name);
}

static bool load_sprite(loader* l, const where* at, const sprat_atlas* atlases,
                        size_t atlas_count, sprat_sprite* s) {
    int atlas_index;
    int h_mode;
    int v_mode;
    size_t marker_count;
    where rect = at_field(at, "rect");
    where trim = at_field(at, "trim");
    where pivot = at_field(at, "pivot");
    where slice = at_field(at, "slice");

    if (!read_tag(l, at, "sprite")) return false;
    if (!read_in_range(l, at, "atlas_index", 0, INT_MAX, &atlas_index)) return false;
    if ((size_t)atlas_index >= atlas_count) {
        where field = at_field(at, "atlas_index");
        return schema_error(l, &field, l->in->token_start,
                            "%d is out of range (the sheet has %zu atlas%s)", atlas_index,
                            atlas_count, atlas_count == 1 ? "" : "es");
    }
    s->atlas_index = (size_t)atlas_index;
    if (!read_size(l, &rect, "x", &s->rect.x)) return false;
    size_t rect_pos = l->in->token_start;
    if (!read_size(l, &rect, "y", &s->rect.y)
        || !read_size(l, &rect, "w", &s->rect.w)
        || !read_size(l, &rect, "h", &s->rect.h)
        || !read_int(l, at, "rotation", &s->rotation)) {
        return false;
    }
    if (s->rotation != 0 && s->rotation != 90 && s->rotation != 270) {
        where field = at_field(at, "rotation");
        return schema_error(l, &field, l->in->token_start, "must be 0, 90 or 270, found %d",
                            s->rotation);
    }
    if (!read_size(l, &trim, "left", &s->trim.left)) return false;
    size_t trim_pos = l->in->token_start;
    if (!read_size(l, &trim, "top", &s->trim.top)
        || !read_size(l, &trim, "right", &s->trim.right)
        || !read_size(l, &trim, "bottom", &s->trim.bottom)
        || !read_coord(l, &pivot, "x", &s->pivot.x)
        || !read_coord(l, &pivot, "y", &s->pivot.y)
        || !read_bool(l, at, "has_slice", &s->has_slice)
        || !read_size(l, &slice, "left", &s->slice.left)) {
        return false;
    }
    size_t slice_pos = l->in->token_start;
    if (!read_size(l, &slice, "top", &s->slice.top)
        || !read_size(l, &slice, "right", &s->slice.right)
        || !read_size(l, &slice, "bottom", &s->slice.bottom)
        || !read_enum(l, &slice, "h_mode", SPRAT_SLICE_FIXED + 1, &h_mode)
        || !read_enum(l, &slice, "v_mode", SPRAT_SLICE_FIXED + 1, &v_mode)
        || !read_count(l, at, "marker_count", &marker_count)) {
        return false;
    }
    size_t marker_count_pos = l->in->token_start;
    if (!read_string(l, at, "name", &s->name)
        || !read_string(l, at, "path", &s->path)
        || !read_optional_string(l, at, "alias_of", &s->alias_of)) {
        return false;
    }
    s->slice.h_mode = (sprat_slice_mode)h_mode;
    s->slice.v_mode = (sprat_slice_mode)v_mode;
    if (!s->has_slice) {
        memset(&s->slice, 0, sizeof(s->slice));
    }

    const sprat_atlas* atlas = &atlases[s->atlas_index];
    /* Compared without adding so huge values cannot overflow past the check. */
    if (s->rect.x > atlas->width || s->rect.w > atlas->width - s->rect.x) {
        return schema_error(l, &rect, rect_pos,
                            "x %d + w %d extends beyond the atlas width %d", s->rect.x,
                            s->rect.w, atlas->width);
    }
    if (s->rect.y > atlas->height || s->rect.h > atlas->height - s->rect.y) {
        return schema_error(l, &rect, rect_pos,
                            "y %d + h %d extends beyond the atlas height %d", s->rect.y,
                            s->rect.h, atlas->height);
    }

    /* Each term is at most SPRAT_COORD_LIMIT, so these sums cannot overflow. */
    sprat_size content = sprat_sprite_content_size(s);
    long long source_w = (long long)content.w + s->trim.left + s->trim.right;
    long long source_h = (long long)content.h + s->trim.top + s->trim.bottom;
    if (source_w > SPRAT_COORD_LIMIT || source_h > SPRAT_COORD_LIMIT) {
        return schema_error(l, &trim, trim_pos,
                            "the source size %lldx%lld (content plus trim) is past "
                            "SPRAT_COORD_LIMIT %d", source_w, source_h, SPRAT_COORD_LIMIT);
    }
    if (s->has_slice && (long long)s->slice.left + s->slice.right > source_w) {
        return schema_error(l, &slice, slice_pos,
                            "left %d + right %d is wider than the source width %lld",
                            s->slice.left, s->slice.right, source_w);
    }
    if (s->has_slice && (long long)s->slice.top + s->slice.bottom > source_h) {
        return schema_error(l, &slice, slice_pos,
                            "top %d + bottom %d is taller than the source height %lld",
                            s->slice.top, s->slice.bottom, source_h);
    }

    if (marker_count > 0) {
        if (marker_count > l->markers_left) {
            where field = at_field(at, "marker_count");
            return schema_error(l, &field, marker_count_pos,
                                "%zu is more than the %zu left of the sheet line's marker_total (%zu)",
                                marker_count, l->markers_left, l->declared.markers);
        }
        sprat_marker* out = l->markers;
        l->markers += marker_count;
        l->markers_left -= marker_count;
        for (size_t i = 0; i < marker_count; ++i) {
            where marker = at_index(at, "markers", i);
            if (!load_marker(l, &marker, &out[i])) return false;
        }
        s->markers = out;
        s->marker_count = marker_count;
    }
    return true;
}

/* ---- Sprite paths and aliases ------------------------------------------ */

static bool same_region(const sprat_sprite* a, const sprat_sprite* b) {
    return a->atlas_index == b->atlas_index && a->rotation == b->rotation
        && a->rect.x == b->rect.x && a->rect.y == b->rect.y
        && a->rect.w == b->rect.w && a->rect.h == b->rect.h;
}

/* Bottom-up merge sort of sprite indexes by path; sprites with equal paths keep
 * their file order. Returns whichever of items and scratch (count entries each)
 * ends up holding the result. */
static size_t* sort_by_path(const sprat_sprite* sprites, size_t* items, size_t* scratch,
                            size_t count) {
    for (size_t width = 1; width < count; width *= 2) {
        for (size_t lo = 0; lo < count; lo += 2 * width) {
            size_t mid = count - lo > width ? lo + width : count;
            size_t hi = count - mid > width ? mid + width : count;
            size_t a = lo;
            size_t b = mid;
            size_t k = lo;
            while (a < mid && b < hi) {
                bool take_b = strcmp(sprites[items[b]].path, sprites[items[a]].path) < 0;
                scratch[k++] = take_b ? items[b++] : items[a++];
            }
            while (a < mid) scratch[k++] = items[a++];
            while (b < hi) scratch[k++] = items[b++];
        }
        size_t* t = items;
        items = scratch;
        scratch = t;
    }
    return items;
}

/* The sprite with that path, or NO_INDEX. */
static size_t find_path(const sprat_sprite* sprites, const size_t* sorted, size_t count,
                        const char* path) {
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(sprites[sorted[mid]].path, path) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count && strcmp(sprites[sorted[lo]].path, path) == 0 ? sorted[lo] : NO_INDEX;
}

/* Index slots kept on the stack (4 KiB on 64-bit): the sorted indexes and the
 * merge sort's scratch for sheets of up to 256 sprites; larger ones allocate
 * them for the check. */
#define PATH_STACK_SLOTS 512

/* Why sprites[i]'s alias has no canonical sprite, or true when it has one. */
static bool check_alias(loader* l, const sprat_sprite* sprites, const size_t* sorted,
                        size_t count, size_t i) {
    const sprat_sprite* alias = &sprites[i];
    where element = at_index(NULL, "sprites", i);
    where field = at_field(&element, "alias_of");
    char path[64];
    sprat_quote(path, sizeof(path), alias->alias_of, strlen(alias->alias_of));
    size_t found = find_path(sprites, sorted, count, alias->alias_of);
    if (found == NO_INDEX) {
        return schema_error(l, &field, NO_POS, "no sprite has the path %s", path);
    }
    const sprat_sprite* c = &sprites[found];
    if (c->alias_of != NULL) {
        return schema_error(l, &field, NO_POS,
                            "%s is sprites[%zu], itself an alias; an alias names a regular sprite",
                            path, found);
    }
    if (!same_region(alias, c)) {
        return schema_error(l, &field, NO_POS,
                            "%s is in atlas %zu at %d,%d %dx%d rotated %d, but this alias is in "
                            "atlas %zu at %d,%d %dx%d rotated %d",
                            path, c->atlas_index, c->rect.x, c->rect.y, c->rect.w, c->rect.h,
                            c->rotation, alias->atlas_index, alias->rect.x, alias->rect.y,
                            alias->rect.w, alias->rect.h, alias->rotation);
    }
    return true;
}

/* A path identifies one sprite, so sprat_sheet_find_sprite_by_path() and
 * binding by path are exact; and every alias names a regular sprite that shares
 * its atlas region, so code that follows alias_of always finds one. Sorting by
 * path makes both checks O(n log n), however hostile the file. The records are
 * gone by now, so errors show the values involved instead of a position. */
static bool check_sprite_paths(loader* l, const sprat_sprite* sprites, size_t count) {
    if (count == 1 && sprites[0].alias_of == NULL) return true;
    if (count > SIZE_MAX / 2 / sizeof(size_t)) return nomem(l);
    const sprat_memory* memory = l->memory;
    size_t stack_slots[PATH_STACK_SLOTS];
    size_t* slots = stack_slots;
    if (count * 2 > PATH_STACK_SLOTS) {
        slots = memory->malloc_func(count * 2 * sizeof(*slots));
        if (slots == NULL) return nomem(l);
    }
    for (size_t i = 0; i < count; ++i) slots[i] = i;
    const size_t* sorted = sort_by_path(sprites, slots, slots + count, count);

    /* Of every pair of sprites with one path, report the later one that comes
     * first in the file, against the sprite before it. */
    size_t duplicate = NO_INDEX;
    size_t original = NO_INDEX;
    for (size_t k = 1; k < count; ++k) {
        if (strcmp(sprites[sorted[k - 1]].path, sprites[sorted[k]].path) == 0
            && sorted[k] < duplicate) {
            duplicate = sorted[k];
            original = sorted[k - 1];
        }
    }
    bool ok = true;
    if (duplicate != NO_INDEX) {
        where element = at_index(NULL, "sprites", duplicate);
        where field = at_field(&element, "path");
        char path[64];
        sprat_quote(path, sizeof(path), sprites[duplicate].path, strlen(sprites[duplicate].path));
        ok = schema_error(l, &field, NO_POS, "%s is also the path of sprites[%zu]; paths are unique",
                          path, original);
    }
    for (size_t i = 0; i < count && ok; ++i) {
        if (sprites[i].alias_of != NULL) ok = check_alias(l, sprites, sorted, count, i);
    }
    if (slots != stack_slots) memory->free_func(slots);
    return ok;
}

/* ---- Animations --------------------------------------------------------- */

/* An alias animation's source, as an index into the animations; -1 for none. */
typedef struct animation_alias {
    int source;
    unsigned char state; /* alias_state; the block starts zeroed, so it starts unseen */
    size_t source_pos;   /* input offset of the source index, for errors */
} animation_alias;

enum alias_state { ALIAS_UNSEEN, ALIAS_WALKING, ALIAS_RESOLVED };

static bool load_animation(loader* l, const where* at, size_t sprite_count,
                           sprat_animation* a, animation_alias* alias) {
    int flip;
    int loop = 0;
    size_t count;
    if (!read_tag(l, at, "anim")
        || !read_in_range(l, at, "fps", 1, SPRAT_FPS_LIMIT, &a->fps)
        || !read_enum(l, at, "flip", (int)(SPRAT_FLIP_H | SPRAT_FLIP_V) + 1, &flip)
        || (l->version >= 2
            && !read_enum(l, at, "loop", (int)(SPRAT_LOOP_ONCE | SPRAT_LOOP_PINGPONG) + 1, &loop))
        || !read_int(l, at, "alias_of", &alias->source)) {
        return false;
    }
    alias->source_pos = l->in->token_start;
    if (!read_count(l, at, "frame_count", &count)) return false;
    a->flip = (unsigned)flip;
    a->loop = (unsigned)loop;
    if (alias->source != -1 && count > 0) {
        where field = at_field(at, "frame_count");
        return schema_error(l, &field, l->in->token_start,
                            "an alias has no frames of its own, found %zu", count);
    }
    if (count > 0) {
        if (count > l->frames_left) {
            where field = at_field(at, "frame_count");
            return schema_error(l, &field, l->in->token_start,
                                "%zu is more than the %zu left of the sheet line's frame_total (%zu)",
                                count, l->frames_left, l->declared.frames);
        }
        size_t* frames = l->frames;
        l->frames += count;
        l->frames_left -= count;
        for (size_t i = 0; i < count; ++i) {
            where frame = at_index(at, "frames", i);
            int index;
            if (!sprat_reader_int(l->in, &index)) return name_failure(l, &frame);
            if (index < 0 || (size_t)index >= sprite_count) {
                return schema_error(l, &frame, l->in->token_start,
                                    "%d is not a sprite index (the sheet has %zu sprite%s)",
                                    index, sprite_count, sprite_count == 1 ? "" : "s");
            }
            frames[i] = (size_t)index;
        }
        a->frames = frames;
        a->frame_count = count;
    }
    return read_string(l, at, "name", &a->name);
}

/* Points every alias animation at its source's name, fps, loop, frames and flip,
 * following chains of aliases. Each walk marks the aliases it passes as walking, stops at
 * a regular animation or an already resolved alias, then copies that one's fps, loop and
 * frames back along the chain and marks it resolved. Every alias is walked at
 * most twice, so long chains in a hostile file stay linear; reaching an alias
 * still marked walking means a cycle. */
static bool resolve_animation_aliases(loader* l, sprat_animation* anims,
                                      animation_alias* aliases, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        int source = aliases[i].source;
        if (source == -1) continue;
        if (source < 0 || (size_t)source >= count) {
            where at = at_index(NULL, "animations", i);
            where field = at_field(&at, "alias_of");
            return schema_error(l, &field, aliases[i].source_pos,
                                "%d is not an animation index (the sheet has %zu animation%s)",
                                source, count, count == 1 ? "" : "s");
        }
        anims[i].alias_of = anims[source].name;
    }

    for (size_t i = 0; i < count; ++i) {
        size_t end = i;
        while (aliases[end].source != -1 && aliases[end].state != ALIAS_RESOLVED) {
            if (aliases[end].state == ALIAS_WALKING) {
                where at = at_index(NULL, "animations", i);
                where field = at_field(&at, "alias_of");
                char name[64];
                sprat_quote(name, sizeof(name), anims[end].name, strlen(anims[end].name));
                return schema_error(l, &field, aliases[i].source_pos,
                                    "aliases form a cycle through animations[%zu] %s", end, name);
            }
            aliases[end].state = ALIAS_WALKING;
            end = (size_t)aliases[end].source;
        }
        /* anims[end] is a regular animation or a resolved alias: its fps, loop,
         * frames and flip are final. An alias's flip combines with its
         * source's, so an alias of a mirrored animation mirrors it again:
         * each alias gets the XOR of its own flip and every flip after it. */
        unsigned after = 0;
        for (size_t a = i; a != end; a = (size_t)aliases[a].source) after ^= anims[a].flip;
        for (size_t a = i; a != end; a = (size_t)aliases[a].source) {
            unsigned own = anims[a].flip;
            anims[a].flip = after ^ anims[end].flip;
            after ^= own;
            anims[a].fps = anims[end].fps;
            anims[a].loop = anims[end].loop;
            anims[a].frames = anims[end].frames;
            anims[a].frame_count = anims[end].frame_count;
            aliases[a].state = ALIAS_RESOLVED;
        }
    }
    return true;
}

/* ---- Sheet -------------------------------------------------------------- */

/* Fewest input bytes a record can take: its tag and a separator, then at least
 * two bytes per value ("0 ", or "0:" for a string, whose bytes STRING_BYTES
 * counts). Vertices and frames are counted per value. Checking the declared
 * totals against the rest of the input caps the block at a few times the file
 * size, so a short hostile file cannot request a huge allocation. */
#define MIN_ATLAS_BYTES ((size_t)6 + 5 * 2)
#define MIN_SPRITE_BYTES ((size_t)7 + 23 * 2)
#define MIN_MARKER_BYTES ((size_t)7 + 8 * 2)
#define MIN_VERTEX_BYTES ((size_t)2 * 2)
/* Version 1 has one value fewer (no LOOP), so this stays a lower bound for both. */
#define MIN_ANIM_BYTES ((size_t)5 + 5 * 2)
#define MIN_FRAME_BYTES ((size_t)2)
#define MIN_HEADER_STRING_BYTES ((size_t)2 * 2)

/* *need += count * per, or false if that overflows. */
static bool add_bytes(size_t* need, size_t count, size_t per) {
    if (count > (SIZE_MAX - *need) / per) return false;
    *need += count * per;
    return true;
}

/* The fewest bytes the declared records take, or false past SIZE_MAX. */
static bool bytes_needed(const totals* t, size_t* need) {
    *need = MIN_HEADER_STRING_BYTES;
    return add_bytes(need, t->atlases, MIN_ATLAS_BYTES)
        && add_bytes(need, t->sprites, MIN_SPRITE_BYTES)
        && add_bytes(need, t->markers, MIN_MARKER_BYTES)
        && add_bytes(need, t->vertices, MIN_VERTEX_BYTES)
        && add_bytes(need, t->animations, MIN_ANIM_BYTES)
        && add_bytes(need, t->frames, MIN_FRAME_BYTES)
        && add_bytes(need, t->string_bytes, 1);
}

/* Reads the sheet line up to its totals; its two strings follow once the block
 * they are copied into exists. */
static bool load_header(loader* l, sprat_sheet* sheet, totals* t) {
    static const where k_format = {NULL, "sprat-raw", NO_INDEX};
    int version;
    if (!sprat_reader_tag(l->in, "sprat-raw")) {
        prefix_message(l->error, "not a sprat raw file: ");
        return false;
    }
    if (!read_int(l, &k_format, "version", &version)) return false;
    if (version < SPRAT_RAW_MIN_VERSION || version > SPRAT_RAW_VERSION) {
        where field = at_field(&k_format, "version");
        return schema_error(l, &field, l->in->token_start,
                            "unsupported version %d (this library reads versions %d..%d)",
                            version, SPRAT_RAW_MIN_VERSION, SPRAT_RAW_VERSION);
    }
    l->version = version;
    int string_bytes;
    if (!read_tag(l, &k_sheet, "sheet")) return false;
    l->sheet_line = l->in->token_start;
    if (!read_double(l, &k_sheet, "scale", &sheet->scale)) return false;
    /* Bounded so that 1 / scale and coordinates times scale stay finite and
     * exact enough; huge exponents parse to infinity, and NaN fails both tests. */
    if (!(sheet->scale >= 1.0 / SPRAT_SCALE_LIMIT && sheet->scale <= SPRAT_SCALE_LIMIT)) {
        where field = at_field(&k_sheet, "scale");
        return schema_error(l, &field, l->in->token_start,
                            "must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found %g",
                            sheet->scale);
    }
    if (!read_size(l, &k_sheet, "extrude", &sheet->extrude)
        || !read_bool(l, &k_sheet, "multipack", &sheet->multipack)
        || !read_double(l, &k_sheet, "collision_atlas_scale", &sheet->collision_atlas_scale)) {
        return false;
    }
    double collision = sheet->collision_atlas_scale;
    if (!(collision == 0.0
          || (collision >= 1.0 / SPRAT_SCALE_LIMIT && collision <= SPRAT_SCALE_LIMIT))) {
        where field = at_field(&k_sheet, "collision_atlas_scale");
        return schema_error(l, &field, l->in->token_start,
                            "must be 0 or between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found %g",
                            collision);
    }
    sheet->collision_atlas_scale = collision + 0.0; /* -0 becomes 0 */
    if (!read_count(l, &k_sheet, "atlas_count", &t->atlases)) return false;
    if (t->atlases == 0) {
        where field = at_field(&k_sheet, "atlas_count");
        return schema_error(l, &field, l->in->token_start, "there must be at least one atlas");
    }
    if (!read_count(l, &k_sheet, "sprite_count", &t->sprites)
        || !read_count(l, &k_sheet, "animation_count", &t->animations)
        || !read_count(l, &k_sheet, "marker_total", &t->markers)
        || !read_count(l, &k_sheet, "vertex_total", &t->vertices)
        || !read_count(l, &k_sheet, "frame_total", &t->frames)
        || !read_in_range(l, &k_sheet, "string_byte_total", 0, INT_MAX, &string_bytes)) {
        return false;
    }
    t->string_bytes = (size_t)string_bytes;
    size_t need = 0;
    size_t left = l->in->size - l->in->pos;
    /* The last value in the file needs no separator after it. */
    if (!bytes_needed(t, &need) || need > left + 1) {
        return schema_error(l, &k_sheet, l->sheet_line,
                            "the file is too short for the records it declares "
                            "(they need at least %zu more bytes, %zu are left)",
                            need, left);
    }
    l->declared = *t;
    return true;
}

/* Byte offsets of the sections in a sheet's block, each aligned like malloc. */
typedef struct block_plan {
    size_t atlases;
    size_t sprites;
    size_t markers;
    size_t vertices;
    size_t animations;
    size_t aliases;
    size_t frames;
    size_t strings;
    size_t string_pool; /* bytes in the strings section */
    size_t size;        /* of the whole block */
} block_plan;

/* Appends a section of count * element bytes at the next aligned offset. */
static bool plan_section(size_t* size, size_t count, size_t element, size_t* offset) {
    size_t start = (*size + SPRAT_ALIGN - 1) / SPRAT_ALIGN * SPRAT_ALIGN;
    if (start < *size || count > (SIZE_MAX - start) / element) return false;
    *offset = start;
    *size = start + count * element;
    return true;
}

static bool plan_block(const totals* t, block_plan* p) {
    /* Every string is copied with its NUL: the sheet's two paths, three per atlas
     * and per sprite, and one per marker and per animation. bytes_needed()
     * already bounded every count by the input size, so this cannot overflow. */
    size_t string_count = 2 + 3 * t->atlases + 3 * t->sprites + t->markers + t->animations;
    p->string_pool = t->string_bytes + string_count;
    p->size = sizeof(sheet_storage);
    return plan_section(&p->size, t->atlases, sizeof(sprat_atlas), &p->atlases)
        && plan_section(&p->size, t->sprites, sizeof(sprat_sprite), &p->sprites)
        && plan_section(&p->size, t->markers, sizeof(sprat_marker), &p->markers)
        && plan_section(&p->size, t->vertices, sizeof(sprat_point), &p->vertices)
        && plan_section(&p->size, t->animations, sizeof(sprat_animation), &p->animations)
        && plan_section(&p->size, t->animations, sizeof(animation_alias), &p->aliases)
        && plan_section(&p->size, t->frames, sizeof(size_t), &p->frames)
        && plan_section(&p->size, p->string_pool, 1, &p->strings);
}

/* Fails unless the records used up every section exactly: totals that claim
 * more than the file has are as wrong as ones that claim less. */
static bool check_totals_used(loader* l) {
    const totals* t = &l->declared;
    static const char* const k_names[] = {"marker_total", "vertex_total", "frame_total"};
    static const char* const k_things[] = {"markers", "vertices", "frames"};
    const size_t declared[] = {t->markers, t->vertices, t->frames};
    const size_t left[] = {l->markers_left, l->vertices_left, l->frames_left};
    for (size_t i = 0; i < 3; ++i) {
        if (left[i] != 0) {
            where field = at_field(&k_sheet, k_names[i]);
            return schema_error(l, &field, l->sheet_line, "declares %zu %s, the file has %zu",
                                declared[i], k_things[i], declared[i] - left[i]);
        }
    }
    if (l->in->strings_left != 0) {
        /* Every string has been read, so only unused declared bytes are left. */
        where field = at_field(&k_sheet, "string_byte_total");
        return schema_error(l, &field, l->sheet_line, "declares %zu string bytes, the file has %zu",
                            t->string_bytes, t->string_bytes - l->in->strings_left);
    }
    return true;
}

/* Fills the block: the sheet line's strings, then every record. */
static bool load_body(loader* l, sprat_sheet* sheet, const totals* t, char* block,
                      const block_plan* p) {
    if (!read_optional_string(l, &k_sheet, "normal_atlas_path", &sheet->normal_atlas_path)
        || !read_optional_string(l, &k_sheet, "collision_atlas_path",
                                 &sheet->collision_atlas_path)) {
        return false;
    }

    sprat_atlas* atlases = (sprat_atlas*)(void*)(block + p->atlases);
    for (size_t i = 0; i < t->atlases; ++i) {
        where at = at_index(NULL, "atlases", i);
        if (!load_atlas(l, &at, &atlases[i])) return false;
    }
    sheet->atlases = atlases;
    sheet->atlas_count = t->atlases;

    if (t->sprites > 0) {
        sprat_sprite* sprites = (sprat_sprite*)(void*)(block + p->sprites);
        for (size_t i = 0; i < t->sprites; ++i) {
            where at = at_index(NULL, "sprites", i);
            if (!load_sprite(l, &at, atlases, t->atlases, &sprites[i])) return false;
        }
        if (!check_sprite_paths(l, sprites, t->sprites)) return false;
        sheet->sprites = sprites;
        sheet->sprite_count = t->sprites;
    }

    if (t->animations > 0) {
        sprat_animation* anims = (sprat_animation*)(void*)(block + p->animations);
        /* Only needed while resolving; a few bytes per animation stay in the block. */
        animation_alias* aliases = (animation_alias*)(void*)(block + p->aliases);
        for (size_t i = 0; i < t->animations; ++i) {
            where at = at_index(NULL, "animations", i);
            if (!load_animation(l, &at, t->sprites, &anims[i], &aliases[i])) return false;
        }
        if (!resolve_animation_aliases(l, anims, aliases, t->animations)) return false;
        sheet->animations = anims;
        sheet->animation_count = t->animations;
    }
    return sprat_reader_end(l->in) && check_totals_used(l);
}

/* ---- Public API --------------------------------------------------------- */

/* Loads with one snapshot of the memory functions; the sheet keeps its free. */
static sprat_status load_memory(const sprat_memory* memory, const char* data, size_t size,
                                sprat_sheet** out, sprat_error* error) {
    sprat_reader reader = {data == NULL ? "" : data, size, 0, 0, error, NULL, 0};
    loader l;
    memset(&l, 0, sizeof(l));
    l.in = &reader;
    l.error = error;
    l.memory = memory;

    sprat_sheet header;
    memset(&header, 0, sizeof(header));
    totals t;
    block_plan plan;
    if (!load_header(&l, &header, &t)) return error->status;
    if (!plan_block(&t, &plan)) {
        nomem(&l);
        return error->status;
    }

    /* Zeroed, so every pointer and count a record does not set is NULL or 0. */
    char* block = memory->calloc_func(1, plan.size);
    if (block == NULL) {
        nomem(&l);
        return error->status;
    }
    sheet_storage* storage = (sheet_storage*)(void*)block;
    storage->sheet = header;
    storage->free_func = memory->free_func;
    l.markers = (sprat_marker*)(void*)(block + plan.markers);
    l.markers_left = t.markers;
    l.vertices = (sprat_point*)(void*)(block + plan.vertices);
    l.vertices_left = t.vertices;
    l.frames = (size_t*)(void*)(block + plan.frames);
    l.frames_left = t.frames;
    reader.strings = block + plan.strings;
    reader.strings_left = plan.string_pool;

    if (!load_body(&l, &storage->sheet, &t, block, &plan)) {
        memory->free_func(block);
        return error->status;
    }
    *out = &storage->sheet;
    return SPRAT_OK;
}

sprat_status sprat_sheet_load_memory(const char* data, size_t size, sprat_sheet** out,
                                     sprat_error* error) {
    sprat_error local_error;
    if (error == NULL) {
        error = &local_error;
    }
    memset(error, 0, sizeof(*error));
    if (out == NULL) {
        sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "out must not be NULL");
        return error->status;
    }
    *out = NULL;
    if (data == NULL && size > 0) {
        sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "data is NULL but size is %zu", size);
        return error->status;
    }
    sprat_memory memory = sprat_memory_current();
    return load_memory(&memory, data, size, out, error);
}

/* fopen() for a UTF-8 path. Windows' fopen() reads paths in the ANSI code page,
 * so there the path is widened for _wfopen(); one that is not valid UTF-8 goes
 * to fopen() as before. *file is NULL when the file cannot be opened; false
 * means the wide path could not be allocated. */
static bool open_file(const sprat_memory* memory, const char* path, FILE** file) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (n > 0) {
        wchar_t* wide = memory->malloc_func((size_t)n * sizeof(wchar_t));
        if (wide == NULL) return false;
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n) == n) {
            *file = _wfopen(wide, L"rb");
        }
        memory->free_func(wide);
        return true;
    }
#else
    (void)memory;
#endif
    *file = fopen(path, "rb");
    return true;
}

/* An I/O error naming the file and, when the C library set errno, why. */
static sprat_status io_error(sprat_error* error, const char* what, const char* path, int err) {
    char quoted[144];
    sprat_quote(quoted, sizeof(quoted), path, strlen(path));
    if (err != 0) {
        sprat_set_error(error, SPRAT_ERR_IO, "cannot %s %s: %s", what, quoted, strerror(err));
    } else {
        sprat_set_error(error, SPRAT_ERR_IO, "cannot %s %s", what, quoted);
    }
    return error->status;
}

sprat_status sprat_sheet_load_file(const char* path, sprat_sheet** out, sprat_error* error) {
    sprat_error local_error;
    if (error == NULL) {
        error = &local_error;
    }
    memset(error, 0, sizeof(*error));
    if (out != NULL) {
        *out = NULL;
    }
    if (path == NULL || out == NULL) {
        sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "%s must not be NULL",
                  path == NULL && out == NULL ? "path and out" : path == NULL ? "path" : "out");
        return error->status;
    }

    sprat_memory memory = sprat_memory_current();
    FILE* file = NULL;
    errno = 0;
    if (!open_file(&memory, path, &file)) {
        sprat_set_error(error, SPRAT_ERR_NOMEM, "out of memory opening a file");
        return error->status;
    }
    if (file == NULL) {
        return io_error(error, "open", path, errno);
    }

    /* Size the buffer from the file length when it is known, with one spare byte
     * so the read reaches EOF without growing. Pipes have no length, and a file
     * can change while it is read, so the buffer still grows when it fills up.
     * Lengths past SPRAT_TRUSTED_FILE_LENGTH are not taken at face value: a
     * directory opens on Linux and reports about 2^63, which must fail as an
     * I/O error on the first read, not as an enormous allocation. */
    size_t first_capacity = 64 * 1024;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        if (fseek(file, 0, SEEK_SET) != 0) {
            int err = errno;
            fclose(file);
            return io_error(error, "read", path, err);
        }
        if (length >= 0 && length <= SPRAT_TRUSTED_FILE_LENGTH) {
            first_capacity = (size_t)length + 1;
        }
    }
    clearerr(file);

    size_t capacity = 0;
    size_t size = 0;
    char* data = NULL;
    errno = 0;
    for (;;) {
        if (size == capacity) {
            size_t grown_capacity = capacity == 0 ? first_capacity : capacity * 2;
            if (grown_capacity < capacity) {
                break; /* reported as NOMEM below */
            }
            char* grown = memory.realloc_func(data, grown_capacity);
            if (grown == NULL) {
                break; /* data is still valid and freed below */
            }
            data = grown;
            capacity = grown_capacity;
        }
        size_t n = fread(data + size, 1, capacity - size, file);
        size += n;
        if (n == 0 || size < capacity) {
            if (feof(file) || ferror(file)) break;
        }
    }

    sprat_status status;
    if (ferror(file)) {
        status = io_error(error, "read", path, errno);
    } else if (!feof(file)) {
        sprat_set_error(error, SPRAT_ERR_NOMEM, "out of memory after reading %zu bytes", size);
        status = error->status;
    } else {
        status = load_memory(&memory, data, size, out, error);
    }
    fclose(file);
    if (data != NULL) {
        memory.free_func(data);
    }
    return status;
}

void sprat_sheet_free(sprat_sheet* sheet) {
    if (sheet == NULL) {
        return;
    }
    sheet_storage* storage = (sheet_storage*)(void*)sheet;
    storage->free_func(storage);
}

const sprat_sprite* sprat_sheet_find_sprite(const sprat_sheet* sheet, const char* name) {
    if (sheet == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < sheet->sprite_count; ++i) {
        if (strcmp(sheet->sprites[i].name, name) == 0) return &sheet->sprites[i];
    }
    return NULL;
}

const sprat_sprite* sprat_sheet_find_sprite_by_path(const sprat_sheet* sheet, const char* path) {
    if (sheet == NULL || path == NULL) return NULL;
    for (size_t i = 0; i < sheet->sprite_count; ++i) {
        if (strcmp(sheet->sprites[i].path, path) == 0) return &sheet->sprites[i];
    }
    return NULL;
}

const sprat_animation* sprat_sheet_find_animation(const sprat_sheet* sheet, const char* name) {
    if (sheet == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < sheet->animation_count; ++i) {
        if (strcmp(sheet->animations[i].name, name) == 0) return &sheet->animations[i];
    }
    return NULL;
}

const sprat_marker* sprat_sprite_find_marker(const sprat_sprite* sprite, const char* name) {
    if (sprite == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < sprite->marker_count; ++i) {
        if (strcmp(sprite->markers[i].name, name) == 0) return &sprite->markers[i];
    }
    return NULL;
}

sprat_size sprat_sprite_content_size(const sprat_sprite* sprite) {
    sprat_size size = {0, 0};
    if (sprite == NULL) return size;
    bool sideways = sprite->rotation == 90 || sprite->rotation == 270;
    size.w = sideways ? sprite->rect.h : sprite->rect.w;
    size.h = sideways ? sprite->rect.w : sprite->rect.h;
    return size;
}

sprat_size sprat_sprite_source_size(const sprat_sprite* sprite) {
    sprat_size size = sprat_sprite_content_size(sprite);
    if (sprite == NULL) return size;
    /* Each term fits in int; the sum is clamped rather than overflowed. */
    long long w = (long long)size.w + sprite->trim.left + sprite->trim.right;
    long long h = (long long)size.h + sprite->trim.top + sprite->trim.bottom;
    size.w = w > INT_MAX ? INT_MAX : (int)w;
    size.h = h > INT_MAX ? INT_MAX : (int)h;
    return size;
}

static char ascii_upper(char c) {
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

/* True when the n bytes at s spell word, ignoring ASCII case. */
static bool equals_upper(const char* s, size_t n, const char* word) {
    size_t i = 0;
    for (; i < n && word[i] != '\0'; ++i) {
        if (ascii_upper(s[i]) != word[i]) return false;
    }
    return i == n && word[i] == '\0';
}

/* COM and LPT take a digit, which Windows also accepts as a superscript:
 * U+00B9, U+00B2 and U+00B3, encoded C2 B9, C2 B2 and C2 B3. */
static bool is_port_digit(const char* s, size_t n) {
    if (n == 1) return s[0] >= '0' && s[0] <= '9';
    if (n != 2 || (unsigned char)s[0] != 0xC2) return false;
    unsigned char b = (unsigned char)s[1];
    return b == 0xB9 || b == 0xB2 || b == 0xB3;
}

/* Windows opens a device, not a file, for these names in any folder and with
 * any extension: "nul.png", "a/CON" and "COM1.png" (with a superscript 1) are
 * all devices. */
static bool is_windows_device(const char* component, size_t len) {
    static const char* const k_devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    size_t n = 0;
    while (n < len && component[n] != '.') ++n; /* the name before any extension */
    while (n > 0 && component[n - 1] == ' ') --n;
    for (size_t i = 0; i < sizeof(k_devices) / sizeof(k_devices[0]); ++i) {
        if (equals_upper(component, n, k_devices[i])) return true;
    }
    return n > 3 && (equals_upper(component, 3, "COM") || equals_upper(component, 3, "LPT"))
        && is_port_digit(component + 3, n - 3);
}

/* Windows drops trailing dots and spaces from names, so a component made only
 * of them could turn into "..". Only "." itself is kept. */
static bool is_dots_and_spaces(const char* component, size_t len) {
    if (len == 1 && component[0] == '.') return false;
    for (size_t i = 0; i < len; ++i) {
        if (component[i] != '.' && component[i] != ' ') return false;
    }
    return len > 0;
}

bool sprat_path_is_safe(const char* path) {
    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\') return false;
    const char* component = path;
    for (const char* p = path;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\0' || c == '/' || c == '\\') {
            size_t len = (size_t)(p - component);
            if (is_dots_and_spaces(component, len) || is_windows_device(component, len)) {
                return false; /* "..", "...", ".. ", "nul.png", ... */
            }
            if (c == '\0') return true;
            component = p + 1;
        } else if (c < 0x20 || c == 0x7F || c == ':') {
            /* ':' covers drive letters (C:) and Windows alternate data streams. */
            return false;
        }
    }
}

const char* sprat_status_string(sprat_status status) {
    switch (status) {
    case SPRAT_OK: return "ok";
    case SPRAT_ERR_INVALID_ARG: return "invalid argument";
    case SPRAT_ERR_IO: return "I/O error";
    case SPRAT_ERR_NOMEM: return "out of memory";
    case SPRAT_ERR_SYNTAX: return "syntax error";
    case SPRAT_ERR_SCHEMA: return "invalid sprat metadata";
    case SPRAT_ERR_NOT_FOUND: return "not found";
    case SPRAT_ERR_AMBIGUOUS: return "ambiguous name";
    }
    return "unknown status";
}

const char* sprat_version(void) {
    return SPRAT_VERSION_STRING;
}
