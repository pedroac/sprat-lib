/*
 * sprat-lib — load sprite sheet metadata produced by sprat-cli.
 *
 * Input: the file written by `spratconvert --transform raw`. All geometry is in
 * atlas pixels (spratconvert has already applied the layout scale), with the
 * origin at the atlas' top-left corner and Y pointing down.
 *
 * A loaded sprat_sheet is one immutable block of memory: every pointer inside it
 * stays valid until sprat_sheet_free(). Strings are NUL-terminated UTF-8.
 */
#ifndef SPRAT_SPRAT_H
#define SPRAT_SPRAT_H

#include <stdbool.h>
#include <stddef.h>

#if defined(SPRAT_SHARED)
#  if defined(_WIN32)
#    if defined(SPRAT_BUILDING)
#      define SPRAT_API __declspec(dllexport)
#    else
#      define SPRAT_API __declspec(dllimport)
#    endif
#  else
#    define SPRAT_API __attribute__((visibility("default")))
#  endif
#else
#  define SPRAT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Errors ------------------------------------------------------------ */

typedef enum sprat_status {
    SPRAT_OK = 0,
    SPRAT_ERR_INVALID_ARG, /* NULL or otherwise unusable argument */
    SPRAT_ERR_IO,          /* the file could not be opened or read */
    SPRAT_ERR_NOMEM,       /* an allocation failed or a size overflowed */
    SPRAT_ERR_SYNTAX,      /* the input is not a well-formed sprat raw file */
    SPRAT_ERR_SCHEMA,      /* well-formed, but the values are not valid sprat metadata */
    SPRAT_ERR_NOT_FOUND,   /* sprat_sheet_bind(): a required name is not in the sheet */
    SPRAT_ERR_AMBIGUOUS    /* sprat_sheet_bind(): a name matches more than one entry */
} sprat_status;

/* What went wrong, for people. message names the value at fault after the
 * model, quotes what was found and gives the limit it broke, as in
 *   sprites[3].rect: x 60 + w 8 extends beyond the atlas width 64
 *   sprites[0].trim.left: expected an integer, found "4x"
 * Values on the sheet line are named sheet.<field>; its totals are
 * sheet.marker_total, vertex_total, frame_total and string_byte_total. */
typedef struct sprat_error {
    sprat_status status;
    /* 1-based line and column (in code points) of the value at fault, for
     * every SPRAT_ERR_SYNTAX and for SPRAT_ERR_SCHEMA when the fault is at one
     * place in the input. 0 otherwise, such as for a sprite alias, which is
     * checked once every sprite is loaded and names the values instead. */
    size_t line;
    size_t column;
    char message[192];
} sprat_error;

/* ---- Memory ------------------------------------------------------------ */

typedef void* (*sprat_malloc_func)(size_t size);
typedef void* (*sprat_calloc_func)(size_t count, size_t size);
typedef void* (*sprat_realloc_func)(void* ptr, size_t size);
typedef void (*sprat_free_func)(void* ptr);

/* Replaces malloc, calloc, realloc and free for everything sprat allocates,
 * like SDL_SetMemoryFunctions(). Optional: by default the C library's are used.
 *
 * All four must be set; otherwise nothing changes and false is returned. They
 * must behave like their C counterparts, returning memory aligned for any type
 * or NULL when out of memory. Call this before loading sheets, not while
 * another thread is loading one. Each sheet keeps the functions it was loaded
 * with, so sprat_sheet_free() stays correct after they are changed again. */
SPRAT_API bool sprat_set_memory_functions(sprat_malloc_func malloc_func,
                                          sprat_calloc_func calloc_func,
                                          sprat_realloc_func realloc_func,
                                          sprat_free_func free_func);

/* The functions currently in use. Any argument may be NULL. */
SPRAT_API void sprat_get_memory_functions(sprat_malloc_func* malloc_func,
                                          sprat_calloc_func* calloc_func,
                                          sprat_realloc_func* realloc_func,
                                          sprat_free_func* free_func);

/* ---- Model ------------------------------------------------------------- */

/* Every coordinate, size, inset and pivot in a loaded sheet lies within
 * -SPRAT_COORD_LIMIT..SPRAT_COORD_LIMIT (2^24). Sums and differences of a few
 * of them cannot overflow int, and every one converts to float exactly. */
#define SPRAT_COORD_LIMIT 16777216

/* sheet->scale lies within 1 / SPRAT_SCALE_LIMIT..SPRAT_SCALE_LIMIT (2^-24..2^24),
 * and so does collision_atlas_scale unless it is 0: dividing by either, or
 * multiplying a coordinate by either, gives a finite result. */
#define SPRAT_SCALE_LIMIT 16777216.0

/* An animation's fps lies within 1..SPRAT_FPS_LIMIT; sprite animations run far
 * slower, so a larger value means a corrupt or mistaken file. */
#define SPRAT_FPS_LIMIT 100

typedef struct sprat_point {
    int x;
    int y;
} sprat_point;

typedef struct sprat_size {
    int w;
    int h;
} sprat_size;

typedef struct sprat_rect {
    int x;
    int y;
    int w;
    int h;
} sprat_rect;

/* Pixels trimmed away from each side of the original image. */
typedef struct sprat_trim {
    int left;
    int top;
    int right;
    int bottom;
} sprat_trim;

typedef enum sprat_marker_type {
    SPRAT_MARKER_POINT,     /* x, y */
    SPRAT_MARKER_CIRCLE,    /* x, y, radius */
    SPRAT_MARKER_RECTANGLE, /* x, y, w, h */
    SPRAT_MARKER_POLYGON    /* vertices, at least one (x, y hold the first vertex) */
} sprat_marker_type;

/* A named shape attached to a sprite (pivot, hitbox, ...), in the coordinate
 * space of the original (untrimmed) image, scaled to atlas pixels. */
typedef struct sprat_marker {
    const char* name;
    sprat_marker_type type;
    int x;
    int y;
    int radius;
    int w;
    int h;
    const sprat_point* vertices;
    size_t vertex_count;
} sprat_marker;

typedef enum sprat_slice_mode {
    SPRAT_SLICE_STRETCH,
    SPRAT_SLICE_REPEAT,
    SPRAT_SLICE_MIRROR,
    SPRAT_SLICE_FIXED
} sprat_slice_mode;

/* Nine-slice insets and the fill mode of the stretchable regions per axis, in
 * the original image scaled to atlas pixels. They always fit the image:
 * left + right <= the source width and top + bottom <= the source height
 * (see sprat_sprite_source_size), so the middle region is never negative. */
typedef struct sprat_slice {
    int left;
    int top;
    int right;
    int bottom;
    sprat_slice_mode h_mode;
    sprat_slice_mode v_mode;
} sprat_slice;

typedef struct sprat_atlas {
    /* Image path exactly as spratconvert was given it (--image-path, else
     * --atlas). Engines usually resolve it against the sheet file's folder. */
    const char* path;
    const char* normal_path;    /* NULL when there is no normal map */
    const char* collision_path; /* NULL when there is no collision map */
    int width;
    int height;
} sprat_atlas;

typedef struct sprat_sprite {
    const char* name; /* basename without extension; not necessarily unique */
    const char* path; /* source image path; no two sprites in a sheet share one */
    /* Canonical sprite path for a deduplicated sprite sharing another
     * sprite's atlas region; NULL for regular sprites. When set, a regular
     * sprite with that path and the same atlas_index, rect and rotation exists. */
    const char* alias_of;
    size_t atlas_index;
    /* Region in the atlas, as stored: for a rotated sprite, w and h are the
     * dimensions after rotation. Already in atlas pixels: sheet->scale is
     * applied, so draw at 1.0 / sheet->scale for the art's original size. */
    sprat_rect rect;
    /* Clockwise rotation, in degrees, that was applied to the image when it
     * was packed: 0, 90 or 270. Rotate by the opposite to draw it upright. */
    int rotation;
    sprat_trim trim;
    /* Pivot in original-image coordinates. spratconvert writes 0,0 when no
     * pivot marker applies. */
    sprat_point pivot;
    bool has_slice;
    sprat_slice slice; /* zeroed when has_slice is false */
    const sprat_marker* markers;
    size_t marker_count;
} sprat_sprite;

enum {
    SPRAT_FLIP_H = 1u << 0,
    SPRAT_FLIP_V = 1u << 1
};

/* How an animation plays, as bits; 0 repeats it forward. SPRAT_LOOP_ONCE stops
 * after one pass: on the last frame, or with SPRAT_LOOP_PINGPONG on the first.
 * SPRAT_LOOP_PINGPONG plays forward then backward. */
enum {
    SPRAT_LOOP_ONCE = 1u << 0,
    SPRAT_LOOP_PINGPONG = 1u << 1
};

typedef struct sprat_animation {
    const char* name;
    int fps; /* 1..SPRAT_FPS_LIMIT */
    /* Indexes into sprat_sheet.sprites, in play order. frame_count may be 0:
     * spratconvert keeps animations whose frames all name missing sprites. */
    const size_t* frames;
    size_t frame_count;
    /* Name of the animation this one replays; NULL for regular animations.
     * fps and frames of an alias are already resolved from its source. */
    const char* alias_of;
    /* SPRAT_FLIP_* bits. For an alias, its own flip combined (XOR) with its
     * source's, so an alias of a mirrored animation mirrors it again. */
    unsigned flip;
    /* SPRAT_LOOP_* bits; an alias's are its source's. 0 for sprat-raw 1 files,
     * which carry no loop mode. */
    unsigned loop;
} sprat_animation;

typedef struct sprat_sheet {
    double scale; /* scale the layout was packed at; see SPRAT_SCALE_LIMIT */
    int extrude;
    bool multipack;

    const sprat_atlas* atlases;
    size_t atlas_count;

    const sprat_sprite* sprites;
    size_t sprite_count;

    const sprat_animation* animations;
    size_t animation_count;

    const char* normal_atlas_path;    /* NULL when absent */
    const char* collision_atlas_path; /* NULL when absent */
    double collision_atlas_scale;     /* 0 when there is no collision atlas */
} sprat_sheet;

/* ---- Threads ----------------------------------------------------------- */

/* sprat keeps no state between calls except the memory functions. So any
 * number of threads may load, bind and query sheets at the same time, the
 * same sheet included, since a loaded sheet is never modified. Only
 * sprat_set_memory_functions() must not run while another thread is inside
 * sprat. Each call writes only its own sprat_error, sprat_sheet** and binding
 * indexes, so give each thread its own.
 *
 * One caveat, for I/O error messages only: their reason comes from strerror(),
 * which C99 does not require to be thread-safe. With a C library where it is
 * not, two threads failing to open files at the same moment could race on the
 * reason's text. */

/* ---- Loading ----------------------------------------------------------- */

/* Parses `size` bytes of spratconvert raw output. On success stores the sheet in *out and returns
 * SPRAT_OK. On failure *out is NULL, and `error` (optional) says why. `data` need not be
 * NUL-terminated, and the sheet keeps no reference to it. */
SPRAT_API sprat_status sprat_sheet_load_memory(const char* data, size_t size,
                                               sprat_sheet** out, sprat_error* error);

/* Reads and parses a whole file. `path` is UTF-8, on Windows too. The file is
 * read whole; to cap the size of untrusted input, read it yourself and call
 * sprat_sheet_load_memory(). */
SPRAT_API sprat_status sprat_sheet_load_file(const char* path, sprat_sheet** out,
                                             sprat_error* error);

/* Releases a sheet. NULL is a no-op. */
SPRAT_API void sprat_sheet_free(sprat_sheet* sheet);

/* ---- Queries ----------------------------------------------------------- */

/* Lookups by name, for occasional use: each one scans the sheet. To resolve
 * the names a program needs once, and to be told about a missing or repeated
 * name rather than silently get NULL or the first match, use sprat_sheet_bind().
 *
 * First sprite with that name, or NULL. Names repeat when sprites in different
 * folders share a file name, so prefer paths, which are unique. */
SPRAT_API const sprat_sprite* sprat_sheet_find_sprite(const sprat_sheet* sheet, const char* name);
/* The sprite with that path, or NULL. */
SPRAT_API const sprat_sprite* sprat_sheet_find_sprite_by_path(const sprat_sheet* sheet, const char* path);
/* First animation with that name, or NULL. */
SPRAT_API const sprat_animation* sprat_sheet_find_animation(const sprat_sheet* sheet, const char* name);
/* First marker of the sprite with that name, or NULL. */
SPRAT_API const sprat_marker* sprat_sprite_find_marker(const sprat_sprite* sprite, const char* name);

/* Size of the trimmed image once rotated back upright. */
SPRAT_API sprat_size sprat_sprite_content_size(const sprat_sprite* sprite);
/* Size of the original image, trim margins included. */
SPRAT_API sprat_size sprat_sprite_source_size(const sprat_sprite* sprite);

/* True when `path` is a relative path that cannot leave the folder it is
 * resolved against: not empty, not absolute (no leading / or \, no drive or
 * colon), no ".." component (either separator), no control characters, and
 * nothing Windows would reinterpret: no component made only of dots and spaces
 * other than ".", and no device name such as CON, NUL or COM1 (any case, with
 * or without an extension, and COM/LPT with a superscript 1, 2 or 3 too).
 * The loader keeps paths as written, since a trusted pipeline may use absolute
 * ones; check atlas and image paths with this before opening them when the
 * sheet comes from an untrusted source. */
SPRAT_API bool sprat_path_is_safe(const char* path);

/* Writes `text` with everything that could steer a terminal or reorder a line
 * escaped, for printing or logging names and paths from an untrusted sheet:
 * control characters and bytes that are not UTF-8 as \xNN (a newline as \n),
 * the C1 controls and Unicode's Bidi_Control set as \uNNNN, and the backslash
 * as \\. Other UTF-8 is copied as is, and no quotes are added.
 * Like snprintf, it writes at most `size` bytes, NUL included, and returns the
 * length of the whole escaped text, so a result >= size means it was cut. It
 * never cuts an escape or a character in half. `out` may be NULL when `size`
 * is 0, and a NULL `text` is empty. */
SPRAT_API size_t sprat_escape(char* out, size_t size, const char* text);

/* ---- Binding names ---------------------------------------------------- */

/* Resolves the names a program needs to indexes, once, after loading:
 *
 *   size_t idle, run;
 *   const sprat_binding bindings[] = {
 *       {SPRAT_BIND_SPRITE_PATH, "hero/idle.png", &idle, 0},
 *       {SPRAT_BIND_ANIMATION, "run", &run, 0},
 *   };
 *   if (sprat_sheet_bind(sheet, bindings, 2, &error) != SPRAT_OK) ...
 *
 * Indexes, unlike pointers, stay meaningful after sprat_sheet_free(): an engine
 * adapter that converts sheet->sprites[i] into its own frames[i], keeping the
 * order, can use them as they are, and animation frames are sprite indexes too.
 * So a program can bind, convert, free the sheet, and never look a name up again. */

typedef enum sprat_bind_kind {
    SPRAT_BIND_SPRITE,      /* a sprite by name, the basename without extension */
    SPRAT_BIND_SPRITE_PATH, /* a sprite by path, which also tells apart equal names */
    SPRAT_BIND_ANIMATION    /* an animation by name */
} sprat_bind_kind;

/* sprat_binding.flags */
enum {
    /* A missing name is not an error: *index becomes SPRAT_NO_INDEX. */
    SPRAT_BIND_OPTIONAL = 1u << 0
};

/* The index of an optional binding whose name is not in the sheet. */
#define SPRAT_NO_INDEX ((size_t)-1)

typedef struct sprat_binding {
    sprat_bind_kind kind;
    const char* name;  /* compared byte for byte, like the find functions */
    size_t* index;     /* receives the index into sheet->sprites or sheet->animations */
    unsigned flags;    /* SPRAT_BIND_* flags, or 0 */
} sprat_binding;

/* Resolves every binding, or none: on failure no index is written.
 *
 * Fails with SPRAT_ERR_NOT_FOUND when a required name is missing, and with
 * SPRAT_ERR_AMBIGUOUS when a name matches more than one entry, optional or
 * not, since taking the first would hide the mistake; bind such a sprite by
 * path. The message names the first failing binding, says how many fail, and
 * suggests a fix where it can, such as a name passed as a path. SPRAT_ERR_INVALID_ARG
 * covers a NULL sheet, name or index, an unknown kind or flag, and NULL
 * bindings with a nonzero count.
 *
 * Each kind that is bound is sorted once and each name found by binary search,
 * so binding thousands of names costs little more than binding a few. That
 * takes one index per binding and two per entry of the largest kind bound: on
 * the stack up to 512 in all (about 256 entries),
 * otherwise a temporary allocation through the memory functions, which fails
 * with SPRAT_ERR_NOMEM. */
SPRAT_API sprat_status sprat_sheet_bind(const sprat_sheet* sheet, const sprat_binding* bindings,
                                        size_t count, sprat_error* error);

SPRAT_API const char* sprat_status_string(sprat_status status);
SPRAT_API const char* sprat_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SPRAT_SPRAT_H */
