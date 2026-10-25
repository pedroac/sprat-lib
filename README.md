# sprat-lib

A small C99 library for loading sprite sheet metadata produced by
[sprat-cli](https://github.com/pedroac/sprat-cli) into your game or engine.

It reads the file written by `spratconvert --transform raw` (sprat-cli v0.20.0 or
later), a plain-token format made to be loaded in one fast pass, including atlases
(multipack, normal and collision maps), sprites (rects, trims, rotation, pivots,
nine-slices, deduplicated aliases), markers, and animations. The library has no
dependencies beyond the C standard library.

```sh
spratlayout ./frames --trim-transparent > layout.txt
spratpack < layout.txt > atlas.png
spratconvert --transform raw -a atlas.png < layout.txt > atlas.sprat
```

```c
#include <sprat/sprat.h>

sprat_sheet* sheet;
sprat_error error;
if (sprat_sheet_load_file("atlas.sprat", &sheet, &error) != SPRAT_OK) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
}

const sprat_animation* run = sprat_sheet_find_animation(sheet, "run");
const sprat_sprite* frame = &sheet->sprites[run->frames[0]];
const sprat_atlas* atlas = &sheet->atlases[frame->atlas_index];
/* frame->rect is the region inside atlas->path to draw. */

sprat_sheet_free(sheet);
```

## Building

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Options:

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library instead of a static one |
| `SPRAT_BUILD_EXAMPLES` | `ON` when top level | Build `sprat-dump` |
| `SPRAT_SANITIZE` | `OFF` | Build with AddressSanitizer and UBSan (GCC/Clang) |

To use it from another CMake project, either call `add_subdirectory(sprat-lib)`
or install it and call `find_package(sprat)`. Both give you the `sprat::sprat`
target. An install also writes `sprat.pc`, for `pkg-config --cflags --libs sprat`.

You can also skip CMake and compile `src/*.c` with `include/` on the include
path. When you link the shared library that way, define `SPRAT_SHARED`.

`sprat-dump sheet.sprat` prints what the library loaded.

## The model

`sprat_sheet` is a plain read-only struct with arrays and counts. A loaded sheet
is stored in one block, so every pointer inside it stays valid until
`sprat_sheet_free()`. Optional strings (`alias_of`, the normal and collision
map paths) are `NULL` when absent.

Coordinates use the same space as spratconvert's output: atlas pixels, with the
origin at the top-left and Y pointing down. The layout's `scale` has already been
applied.

- **Sprites**: `rect` is the region in the atlas, as stored. For a rotated sprite
  (`rotation` is 90 or 270, the clockwise rotation applied when it was packed),
  `rect.w`/`rect.h` are the dimensions after rotation, and
  `sprat_sprite_content_size()` gives the upright size. `trim` holds the margins
  removed from the original image, and `sprat_sprite_source_size()` adds them
  back. `pivot` and markers are in original-image coordinates.
- **Names** are file basenames, so they can repeat across folders. **Paths**
  are unique within a sheet (the loader rejects a repeat), so use
  `sprat_sheet_find_sprite_by_path()` or bind by path when you need an exact sprite.
- **Atlas paths** are written exactly as `spratconvert` was given them
  (`--image-path`, else `--atlas`), and are usually meant relative to the
  `.sprat` file. Engines such as raylib resolve relative paths against the
  working directory, so join `atlas->path` with the sheet's folder before
  loading the texture.
- **Aliases**: a deduplicated sprite has `alias_of` set to its canonical path
  and shares that sprite's atlas region, but keeps its own trim.
- **Animations** list sprite indexes in play order. An alias animation (`alias_of`
  set) already has its source's `fps` and `frames` filled in. Its `flip` bits
  combine (XOR) its own flip with its source's, so an alias of a mirrored
  animation mirrors it again, along chains of aliases too.

## Lifetime and binding names

A sheet is read-only and can stay loaded for as long as you like. When the game
only needs part of it, the usual pattern is load, bind, convert, free:

1. Load the sheet.
2. Bind the names the game uses to indexes with `sprat_sheet_bind()`.
3. Load each atlas texture from `atlas->path`, and copy what the game draws
   with into its own structures, in the sheet's order.
4. Call `sprat_sheet_free()`.

Names then cost nothing at run time. There is no call that frees part of a
sheet: it is one block, and its pointers stay valid until it is freed whole.

`sprat_sheet_bind()` resolves a table of names in one call, all or nothing:

```c
size_t idle, run, coin;
const sprat_binding bindings[] = {
    {SPRAT_BIND_SPRITE_PATH, "hero/idle.png", &idle, 0},
    {SPRAT_BIND_ANIMATION,   "run",           &run,  0},
    {SPRAT_BIND_SPRITE,      "coin",          &coin, SPRAT_BIND_OPTIONAL},
};
if (sprat_sheet_bind(sheet, bindings, 3, &error) != SPRAT_OK) {
    /* bindings[1]: no animation is named "run" (the sheet has 2 animations: "walk", "jump") */
}
```

- It gives **indexes**, not pointers, so they stay meaningful after the sheet is
  freed. Animation frames are sprite indexes too.
- A missing name fails with `SPRAT_ERR_NOT_FOUND`, unless the binding is
  `SPRAT_BIND_OPTIONAL`: then its index is `SPRAT_NO_INDEX`.
- A name that matches more than one entry fails with `SPRAT_ERR_AMBIGUOUS`,
  optional or not, rather than silently taking the first. Sprite names are
  basenames and repeat across folders, so bind those by path.
- The message names the first failing binding, says how many fail, and points
  out a name passed as the wrong kind, such as a path bound as a name.
- On failure no index is written.
- Each kind that is bound is sorted once, so binding thousands of names is
  cheap. That needs one index per binding plus two per entry of the largest
  kind bound, kept on the stack up to 512 in all (about 256 entries); past that
  it takes a temporary allocation, which can fail with `SPRAT_ERR_NOMEM`.

**Engine adapters keep the sheet's order.** A library that turns a sheet into
engine textures and rectangles should store `sheet->sprites[i]` as its own
`frames[i]`, and `sheet->animations[i]` as its own `animations[i]`. Then the
indexes from `sprat_sheet_bind()`, and the frame indexes inside animations,
work on the adapter's data unchanged, after the sheet is freed. An adapter can
take a binding table from its caller and pass it straight through.

```c
typedef struct game_frame {
    int texture;          /* the engine's handle for the atlas */
    float x, y, w, h;     /* source rect in the atlas */
    float pivot_x, pivot_y;
    int trim_left, trim_top, rotation;
} game_frame;

typedef struct game_sprites {
    game_frame* frames;   /* one per sheet sprite, same order */
    size_t frame_count;
    size_t* run;          /* the "run" animation, as frame indexes */
    size_t run_count;
    int run_fps;
    size_t idle;          /* a frame index, bound by path */
} game_sprites;

bool load_game_sprites(const char* path, game_sprites* out) {
    sprat_sheet* sheet;
    sprat_error error;
    if (sprat_sheet_load_file(path, &sheet, &error) != SPRAT_OK) {
        fprintf(stderr, "%s: %s\n", path, error.message);
        return false;
    }

    size_t run;
    const sprat_binding bindings[] = {
        {SPRAT_BIND_ANIMATION, "run", &run, 0},
        {SPRAT_BIND_SPRITE_PATH, "hero/idle.png", &out->idle, 0},
    };
    if (sprat_sheet_bind(sheet, bindings, 2, &error) != SPRAT_OK) {
        fprintf(stderr, "%s: %s\n", path, error.message);
        sprat_sheet_free(sheet);
        return false;
    }

    /* Atlas paths are usually relative to the sheet's folder. */
    int textures[16];
    for (size_t i = 0; i < sheet->atlas_count && i < 16; ++i) {
        textures[i] = load_texture_next_to(path, sheet->atlases[i].path);
    }

    out->frame_count = sheet->sprite_count;
    out->frames = malloc(sheet->sprite_count * sizeof(*out->frames));
    for (size_t i = 0; i < sheet->sprite_count; ++i) {
        const sprat_sprite* s = &sheet->sprites[i];
        out->frames[i] = (game_frame){
            textures[s->atlas_index],
            (float)s->rect.x, (float)s->rect.y, (float)s->rect.w, (float)s->rect.h,
            (float)s->pivot.x, (float)s->pivot.y,
            s->trim.left, s->trim.top, s->rotation,
        };
    }

    /* Frame indexes are sprite indexes, so they index out->frames as they are. */
    const sprat_animation* a = &sheet->animations[run];
    out->run_count = a->frame_count;
    out->run = NULL; /* an animation may have no frames, and then a->frames is NULL */
    if (a->frame_count > 0) {
        out->run = malloc(a->frame_count * sizeof(size_t));
        memcpy(out->run, a->frames, a->frame_count * sizeof(size_t));
    }
    out->run_fps = a->fps;

    sprat_sheet_free(sheet); /* nothing above points into it */
    return true;
}
```

Error handling for `malloc` and more than 16 atlases is left out for brevity.
Copy values, never pointers: `name`, `frames` and every other pointer in the
sheet dangle once it is freed.

## Drawing a sprite

sprat-lib only loads metadata. Engine integrations (SDL, raylib, ...) belong in
separate libraries, and they all place a sprite the same way. To draw `sprite`
with its pivot at world point `W` (Y down, angles clockwise), magnified by
`draw_scale`:

```
c   = sprat_sprite_content_size(sprite)          /* upright trimmed size */
d.x = sprite->trim.left + c.w / 2.0 - sprite->pivot.x
d.y = sprite->trim.top  + c.h / 2.0 - sprite->pivot.y
if (flip & SPRAT_FLIP_H) d.x = -d.x              /* mirror about the pivot */
if (flip & SPRAT_FLIP_V) d.y = -d.y

source = sprite->rect                            /* as stored in the atlas */
size   = (sprite->rect.w, sprite->rect.h)        /* as stored, not upright */
center = W + d * draw_scale
angle  = -sprite->rotation                       /* undoes the packing turn */
```

`draw_scale` is yours to choose, and it is not `sheet->scale`. Geometry is
already in atlas pixels, so `draw_scale = 1` draws the atlas pixels one to one.
For a sheet packed at `sheet->scale` 0.5, `draw_scale = 1.0 / sheet->scale`
draws the art at its original size.

Draw `source` into a box of `size * draw_scale` centered on `center`, rotated by
`angle` around that center. This also covers sprites with `rotation` 0. For
`rotation` 90 or 270, swap the `SPRAT_FLIP_H` and `SPRAT_FLIP_V` bits before
handing them to the engine, because engines flip the stored image before they
rotate it.

- raylib: `DrawTexturePro(tex, source, (Rectangle){center.x, center.y, w, h},
  (Vector2){w / 2, h / 2}, angle, tint)`. Flip with a negative `source.width`
  or `source.height`.
- SDL3: `SDL_RenderTextureRotated` with `dst` centered on `center`, `angle`, a
  `NULL` center (the middle of `dst`) and `(SDL_FlipMode)flip`. The
  `SPRAT_FLIP_*` bits have the same values as `SDL_FLIP_HORIZONTAL`,
  `SDL_FLIP_VERTICAL` and `SDL_FLIP_HORIZONTAL_AND_VERTICAL`.

A marker point `m` (or a polygon vertex) lands at `W + (m - pivot) * draw_scale`,
with the x offset negated for `SPRAT_FLIP_H` and the y offset for
`SPRAT_FLIP_V`, the same mirror as the sprite.

Nine-slice insets are in original-image space, while raylib's
`DrawTextureNPatch` and SDL3's `SDL_RenderTexture9Grid` take insets relative to
the packed `source`. For an unrotated sprite, subtract the trim on each side
(`slice.left - trim.left`, and so on, clamped at 0). A rotated sprite's insets
no longer line up with its atlas region, so draw its nine regions yourself.

Example: a 10x6 image trimmed by 1 left, 2 top and 3 right becomes 6x4. Packed
with `rotation` 90 it has `rect.w` 4 and `rect.h` 6. With pivot (5, 3) drawn at
W = (100, 50), `d` is (-1, 1). That gives a 4x6 box centered on (99, 51). Once
turned by -90 degrees it covers x 96..102 and y 49..53, so the content's
top-left is at W - pivot + (trim.left, trim.top) = (96, 49).

## Errors

Every loader returns a `sprat_status` and, when you pass a `sprat_error`, fills
it with a message. The message names the value at fault the way the model does,
quotes what was found, and gives the limit it broke:

```
sheet.sprat:4:14: sprites[0].rect.w: expected an integer, found "4x"
sheet.sprat:4:10: sprites[0].rect: x 60 + w 8 extends beyond the atlas width 64
sheet.sprat:6:13: animations[1].alias_of: 5 is not an animation index (the sheet has 2 animations)
sheet.sprat: invalid sprat metadata: sprites[7].alias_of: no sprite has the path "hero/run_01.png"
```

`line` and `column` (1-based, columns in code points) point at the value for
every syntax error, and for schema errors too wherever the fault sits at one
place in the input. They are 0 otherwise: a sprite alias is checked once every
sprite is loaded, so its message shows the values involved instead. Values on
the sheet line are named `sheet.<field>`, and its totals `sheet.marker_total`,
`vertex_total`, `frame_total` and `string_byte_total`. I/O errors quote the
path and give the C library's reason.

Messages quote text from the file safely: control characters, invalid bytes,
C1 controls such as U+009B and bidirectional overrides such as U+202E are
escaped (`\x1B`, `\u202E`), so a hostile sheet cannot slip terminal escape
sequences or reordered text into your logs.

The loader validates everything code using a sheet would otherwise have to
check, so a loaded sheet can be used without further checks:

- **Indexes and references:** atlas indexes, animation frame indexes, animation
  aliases (including cycles; an alias has no frames of its own), and sprite
  aliases (`alias_of` always names a regular sprite with the same atlas region).
  No two sprites share a path.
- **Ranges:** sprite rects lie inside their atlas; every coordinate, size,
  inset, pivot and marker value is within `±SPRAT_COORD_LIMIT` (2^24), so a few
  of them can be added without overflowing `int` and each converts to `float`
  exactly; slice insets fit the source image; `scale` lies within
  2^-24..2^24 (`SPRAT_SCALE_LIMIT`), and `collision_atlas_scale` too unless it
  is 0, so dividing by either stays finite; `fps` lies within 1..100
  (`SPRAT_FPS_LIMIT`); polygons have at least one vertex.
- **Enums and strings:** enum values, valid UTF-8 without NUL bytes, and counts
  against the input size, so a corrupt file cannot request a huge allocation.

The fps limit keeps arithmetic such as `elapsed_ms * fps` in an `int` safe for
about 6 hours of elapsed time, not forever: wrap the time you keep per
animation, or use 64-bit math, if an animation may play for longer.

An animation may have no frames (spratconvert keeps one whose frames all name
missing sprites), so check `frame_count` before taking a frame modulo it.

Paths are kept as written, because a trusted pipeline may use absolute ones.
When a sheet comes from an untrusted source, check each atlas or image path with
`sprat_path_is_safe()` before opening it: it accepts only relative paths with no
`..` component, drive, colon or control character, and nothing Windows would
reinterpret (a component of only dots and spaces, or a device name such as
`CON` or `nul.png`).

## File format

The format is specified at the top of sprat-cli's `transforms/raw.jsonnet`. In
short: one record per line (`sheet`, `atlas`, `sprite`, `marker`, `anim`), every
value a space-separated token, and strings written as `<UTF-8 byte count>:<bytes>`,
so names and paths may contain spaces. Each count comes before the records it
counts, and the `sheet` line also declares the file's totals: markers, polygon
vertices, animation frames and string bytes. The loader rejects a file whose
records don't match them exactly. The first line, `sprat-raw 1`, carries the
format version; the loader rejects other versions with `SPRAT_ERR_SCHEMA`.

## Memory

sprat uses `malloc`, `calloc`, `realloc` and `free` by default. To route its
allocations through your own functions, set them once at startup, as with
SDL's `SDL_SetMemoryFunctions()`:

```c
sprat_set_memory_functions(my_malloc, my_calloc, my_realloc, my_free);
```

SDL3's functions have exactly these signatures, so
`sprat_set_memory_functions(SDL_malloc, SDL_calloc, SDL_realloc, SDL_free)` works
as is. raylib's `MemAlloc` and `MemRealloc` take an `unsigned int` size, so wrap
them in functions with the signatures above.

All four are required; if any is `NULL`, nothing changes and the call returns
`false`. They must behave like the C library's, including alignment. Set them
before loading sheets and not while another thread is loading. Each sheet
remembers the functions it was loaded with, so `sprat_sheet_free()` stays
correct if you change them later. `sprat_get_memory_functions()` returns the
current ones, for example to restore them.

A sheet is **one allocation**. The `sheet` line declares how many records and
string bytes follow, so the loader computes the model's exact size before
reading the records and allocates a single block for it, with nothing unused:
the sheet, then one section each for atlases, sprites, markers, vertices,
animations, frames and strings. `sprat_sheet_free()` releases that one block.
The input is parsed in place and the sheet keeps no reference to it.

`sprat_sheet_load_file()` adds one temporary allocation, a read buffer sized
from the file length and freed before it returns (and, on Windows, a second for
the path widened to UTF-16). Checking that paths are unique and that aliases
name a regular sprite sorts the sprites by path: the index arrays live on the
stack for sheets of up to 256 sprites and are a temporary allocation for larger
ones, freed before the load returns. `sprat_sheet_bind()` works the same way.

Declared totals are untrusted like everything else. Each record type has a
minimum size in bytes, so totals that the rest of the file cannot hold are
rejected before anything is allocated, which caps the block at a few times the
file size. For 20,000 sprites, the sheet takes about 1.6 times the file size.

## Threads

sprat keeps no state between calls except the memory functions. Any number of
threads may load, bind and query sheets at the same time, the same sheet
included, since a loaded sheet is never modified. Only
`sprat_set_memory_functions()` must not run while another thread is inside
sprat. Give each thread its own `sprat_error`, output pointer and binding
indexes.

The one caveat is in I/O error messages: their reason comes from `strerror()`,
which C99 does not require to be thread-safe. With a C library where it is not,
two threads failing to open files at the same moment could race on the reason's
text.

## Limitations

- spratconvert writes a `0,0` pivot when no pivot marker applies, so a
  real pivot at the origin looks the same as no pivot at all.
- Numbers are parsed without `strtod`, so the C locale has no effect. Every
  value sprat-cli writes is parsed exactly. Values with more than 19
  significant digits or extreme exponents may be off by a few ULPs.
- `sprat_sheet_load_file()` takes a UTF-8 path. On Windows it opens it with
  `_wfopen`, falling back to `fopen` for a path that is not valid UTF-8. To read
  through your engine's file system (SDL's `SDL_LoadFile`, raylib's
  `LoadFileData`, Android assets, ...), use `sprat_sheet_load_memory()`.
- The `find_` functions are linear scans, for occasional use. Bind the names
  you need once at load time instead: `sprat_sheet_bind()` sorts once and
  binary-searches every name.

## License

MIT. See [LICENSE](LICENSE).
