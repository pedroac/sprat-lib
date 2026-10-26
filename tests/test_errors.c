/* Every error the loader reports, with its exact message and position, so a
 * change to any message shows up here. Then sweeps over the fixture: cut short
 * at every byte, every token replaced, and seeded random corruption. Each must
 * fail cleanly, name the right value and point at the right place. */
#include "support.h"

typedef struct error_case {
    int source_line; /* of the case in this file, for failure reports */
    const char* text;
    sprat_status status;
    size_t line;
    size_t column;
    const char* message;
} error_case;

#define E(...) {__LINE__, __VA_ARGS__}

/* Sheet lines that keep the "too short" check out of the way of the record
 * after them: the spaces count as input that later records could fill. */
#define PAD "                                                                    \n"

static const error_case k_cases[] = {
    /* ---- The first line ---- */
    E("", SPRAT_ERR_SYNTAX, 1, 1,
      "not a sprat raw file: expected \"sprat-raw\", found the end of the file"),
    E("{\"atlases\":[]}", SPRAT_ERR_SYNTAX, 1, 1,
      "not a sprat raw file: expected \"sprat-raw\", found \"{\\\"atlases\\\":[]}\""),
    E("\n\n  sprat-ra 1", SPRAT_ERR_SYNTAX, 3, 3,
      "not a sprat raw file: expected \"sprat-raw\", found \"sprat-ra\""),
    E("sprat-raw 3\n", SPRAT_ERR_SCHEMA, 1, 11,
      "sprat-raw.version: unsupported version 3 (this library reads versions 1..2)"),
    E("sprat-raw 0\n", SPRAT_ERR_SCHEMA, 1, 11,
      "sprat-raw.version: unsupported version 0 (this library reads versions 1..2)"),
    E("sprat-raw one\n", SPRAT_ERR_SYNTAX, 1, 11,
      "sprat-raw.version: expected an integer, found \"one\""),
    E("sprat-raw", SPRAT_ERR_SYNTAX, 1, 10,
      "sprat-raw.version: expected an integer, found the end of the file"),

    /* ---- The sheet line ---- */
    E("sprat-raw 1\nsheets 1", SPRAT_ERR_SYNTAX, 2, 1,
      "sheet: expected \"sheet\", found \"sheets\""),
    E("sprat-raw 1\nsheet 0 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found 0"),
    E("sprat-raw 1\nsheet -0.5 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found -0.5"),
    E("sprat-raw 1\nsheet 1e999 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found inf"),
    /* Values a game would divide by or multiply with: 1 / 1e-310 is infinite. */
    E("sprat-raw 1\nsheet 1e-310 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found 1e-310"),
    E("sprat-raw 1\nsheet 5.9e-8 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found 5.9e-08"),
    E("sprat-raw 1\nsheet 16777217 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 7,
      "sheet.scale: must be between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), found 1.67772e+07"),
    E("sprat-raw 1\nsheet 1,5 0 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SYNTAX, 2, 7,
      "sheet.scale: expected a number, found \"1,5\""),
    E("sprat-raw 1\nsheet 1 -1 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 9,
      "sheet.extrude: must not be negative, found -1"),
    E("sprat-raw 1\nsheet 1 16777217 0 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 9,
      "sheet.extrude: 16777217 is out of range 0..16777216 (SPRAT_COORD_LIMIT)"),
    E("sprat-raw 1\nsheet 1 0 2 0 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 11,
      "sheet.multipack: must be 0 or 1, found 2"),
    E("sprat-raw 1\nsheet 1 0 0 -1 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 13,
      "sheet.collision_atlas_scale: must be 0 or between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), "
      "found -1"),
    E("sprat-raw 1\nsheet 1 0 0 1e-30 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 13,
      "sheet.collision_atlas_scale: must be 0 or between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), "
      "found 1e-30"),
    E("sprat-raw 1\nsheet 1 0 0 1e999 1 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 13,
      "sheet.collision_atlas_scale: must be 0 or between 2^-24 and 2^24 (SPRAT_SCALE_LIMIT), "
      "found inf"),
    E("sprat-raw 1\nsheet 1 0 0 0 0 0 0 ~ 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 15,
      "sheet.atlas_count: there must be at least one atlas"),
    E("sprat-raw 1\nsheet 1 0 0 0 -1 0 0 ~ 0: 0:\n", SPRAT_ERR_SYNTAX, 2, 15,
      "sheet.atlas_count: expected a count (0 or more), found \"-1\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 4000000000 0 ~ 0: 0:\n", SPRAT_ERR_SYNTAX, 2, 17,
      "sheet.sprite_count: integer out of range (-2147483648 to 2147483647), found \"4000000000\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 999 0 0 0 0 0: 0:\n", SPRAT_ERR_SYNTAX, 2, 19,
      "sheet.animation_count: count is more than the rest of the file can hold, found \"999\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 x 0 0 0 0: 0:\n", SPRAT_ERR_SYNTAX, 2, 21,
      "sheet.marker_total: expected an integer, found \"x\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 -1 0: 0:\n", SPRAT_ERR_SCHEMA, 2, 27,
      "sheet.string_byte_total: must not be negative, found -1"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 5 0 0 0 0 0 0: 0:\natlas 8 8 0: 0: 0:\n", SPRAT_ERR_SCHEMA,
      2, 1,
      "sheet: the file is too short for the records it declares (they need at least 285 more "
      "bytes, 26 are left)"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 0 x 0:\natlas 8 8 0: 0: 0:\n", SPRAT_ERR_SYNTAX,
      2, 29, "sheet.normal_atlas_path: expected a string, found \"x\""),
    /* 5 bytes take the newline and "atla"; the "s" after them is no separator. */
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 0 0: 5:\natlas 8 8 0: 0: 0:\n", SPRAT_ERR_SYNTAX,
      3, 5, "sheet.collision_atlas_path: expected a space after the string, found \"s\""),

    /* ---- Atlases ---- */
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 ~ 0: 0:\natlass 8 8 0: 0: 0:\n" PAD, SPRAT_ERR_SYNTAX,
      3, 1, "atlases[0]: expected \"atlas\", found \"atlass\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 ~ 0: 0:\natlas 0 8 0: 0: 0:\n", SPRAT_ERR_SCHEMA, 3, 7,
      "atlases[0].width: must be at least 1, found 0"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 ~ 0: 0:\natlas 8 16777217 0: 0: 0:\n", SPRAT_ERR_SCHEMA,
      3, 9, "atlases[0].height: 16777217 is out of range 1..16777216 (SPRAT_COORD_LIMIT)"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 ~ 0: 0:\natlas 8 8 x 0: 0:\n" PAD, SPRAT_ERR_SYNTAX, 3,
      11, "atlases[0].path: expected a string, found \"x\""),
    E("sprat-raw 1\nsheet 1 0 0 0 1 0 0 ~ 0: 0:\natlas 8 8 0: 0:\n" PAD, SPRAT_ERR_SYNTAX, 5, 1,
      "atlases[0].collision_path: expected a string, found the end of the file"),

    /* ---- Sprites ---- */
    E(HEADER(1, 0) "sprote 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SYNTAX, 4, 1, "sprites[0]: expected \"sprite\", found \"sprote\""),
    E(HEADER(1, 0) "sprite 1 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 8, "sprites[0].atlas_index: 1 is out of range (the sheet has 1 atlas)"),
    E(HEADER(1, 0) "sprite -1 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 8, "sprites[0].atlas_index: must not be negative, found -1"),
    E(HEADER(1, 0) SPRITE_AT("6 0 4 4", "0 1:a 1:a 0:"), SPRAT_ERR_SCHEMA, 4, 10,
      "sprites[0].rect: x 6 + w 4 extends beyond the atlas width 8"),
    E(HEADER(1, 0) SPRITE_AT("9 0 0 4", "0 1:a 1:a 0:"), SPRAT_ERR_SCHEMA, 4, 10,
      "sprites[0].rect: x 9 + w 0 extends beyond the atlas width 8"),
    E(HEADER(1, 0) SPRITE_AT("0 2 4 7", "0 1:a 1:a 0:"), SPRAT_ERR_SCHEMA, 4, 10,
      "sprites[0].rect: y 2 + h 7 extends beyond the atlas height 8"),
    E(HEADER(1, 0) SPRITE_AT("-1 0 4 4", "0 1:a 1:a 0:"), SPRAT_ERR_SCHEMA, 4, 10,
      "sprites[0].rect.x: must not be negative, found -1"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4x 4", "0 1:a 1:a 0:"), SPRAT_ERR_SYNTAX, 4, 14,
      "sprites[0].rect.w: expected an integer, found \"4x\""),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 45 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 18, "sprites[0].rotation: must be 0, 90 or 270, found 45"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 -90 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 18, "sprites[0].rotation: must be 0, 90 or 270, found -90"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 16777217 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 20,
      "sprites[0].trim.left: 16777217 is out of range 0..16777216 (SPRAT_COORD_LIMIT)"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 -1 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 26, "sprites[0].trim.bottom: must not be negative, found -1"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 16777216 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 20,
      "sprites[0].trim: the source size 16777220x4 (content plus trim) is past "
      "SPRAT_COORD_LIMIT 16777216"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 90 0 16777210 0 7 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 21,
      "sprites[0].trim: the source size 4x16777221 (content plus trim) is past "
      "SPRAT_COORD_LIMIT 16777216"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 -16777217 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 28,
      "sprites[0].pivot.x: -16777217 is out of range -16777216..16777216 (SPRAT_COORD_LIMIT)"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 32, "sprites[0].has_slice: must be 0 or 1, found 2"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 0 0 1 3 0 2 0 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 34,
      "sprites[0].slice: left 3 + right 2 is wider than the source width 4"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 0 0 1 0 2 0 3 0 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 34,
      "sprites[0].slice: top 2 + bottom 3 is taller than the source height 4"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 0 0 1 1 1 1 1 4 0 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 42, "sprites[0].slice.h_mode: unknown value 4 (expected 0..3)"),
    E(HEADER(1, 0) "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 0 1:a 1:a 0:\n",
      SPRAT_ERR_SCHEMA, 4, 44, "sprites[0].slice.v_mode: unknown value -1 (expected 0..3)"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "999999 1:a 1:a 0:"), SPRAT_ERR_SYNTAX, 4, 46,
      "sprites[0].marker_count: count is more than the rest of the file can hold, found "
      "\"999999\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 3:a 1:a 0:"), SPRAT_ERR_SYNTAX, 4, 53,
      "sprites[0].name: expected a space after the string, found \":a\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:\xFF 1:a 0:"), SPRAT_ERR_SYNTAX, 4, 50,
      "sprites[0].name: invalid UTF-8 in string, found \"\\xFF\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:a 1:a 99:"), SPRAT_ERR_SYNTAX, 4, 56,
      "sprites[0].alias_of: string runs past the end of the file, found \"99:\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:a 1:a"), SPRAT_ERR_SYNTAX, 5, 1,
      "sprites[0].alias_of: expected a string, found the end of the file"),

    /* ---- Markers ---- */
    /* Totals written out: "~" would not count a misspelt marker. */
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 0 1 0 0 3 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "markr 0 0 0 0 0 0 0 1:m\n" PAD,
      SPRAT_ERR_SYNTAX, 5, 1, "sprites[0].markers[0]: expected \"marker\", found \"markr\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 7 0 0 0 0 0 0 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 8, "sprites[0].markers[0].type: unknown value 7 (expected 0..3)"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 0 16777217 0 0 0 0 0 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 10,
      "sprites[0].markers[0].x: 16777217 is out of range -16777216..16777216 (SPRAT_COORD_LIMIT)"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 1 0 0 -3 0 0 0 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 14, "sprites[0].markers[0].radius: must not be negative, found -3"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 0 0 0 0 0 0 1 2 2 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 20,
      "sprites[0].markers[0].vertex_count: only polygons have vertices, this is a point"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 1 0 0 1 0 0 1 2 2 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 20,
      "sprites[0].markers[0].vertex_count: only polygons have vertices, this is a circle"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 2 0 0 0 1 1 1 2 2 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 20,
      "sprites[0].markers[0].vertex_count: only polygons have vertices, this is a rectangle"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 0 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 20,
      "sprites[0].markers[0].vertex_count: a polygon needs at least one vertex"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 2 0 0 1 -16777217 1:m\n",
      SPRAT_ERR_SCHEMA, 5, 28,
      "sprites[0].markers[0].vertices[1].y: -16777217 is out of range -16777216..16777216 "
      "(SPRAT_COORD_LIMIT)"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "2 1:a 1:a 0:") "marker 0 0 0 0 0 0 0 1:m\n"
      "marker 0 0 0 0 0 0 0 1:mm\n", SPRAT_ERR_SYNTAX, 6, 25,
      "sprites[0].markers[1].name: expected a space after the string, found \"m\""),

    /* ---- Animations ---- */
    E(HEADER(0, 1) "animation 8 0 -1 0 1:r\n", SPRAT_ERR_SYNTAX, 4, 1,
      "animations[0]: expected \"anim\", found \"animation\""),
    E(HEADER(0, 1) "anim 0 0 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 6,
      "animations[0].fps: must be at least 1, found 0"),
    E(HEADER(0, 1) "anim 101 0 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 6,
      "animations[0].fps: 101 is out of range 1..100 (SPRAT_FPS_LIMIT)"),
    E(HEADER(0, 1) "anim 2147483647 0 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 6,
      "animations[0].fps: 2147483647 is out of range 1..100 (SPRAT_FPS_LIMIT)"),
    E(HEADER(0, 1) "anim 8 4 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 8,
      "animations[0].flip: unknown value 4 (expected 0..3)"),
    E(HEADER_V2(0, 1) "anim 8 0 4 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 10,
      "animations[0].loop: unknown value 4 (expected 0..3)"),
    /* A version 1 record read as version 2 is missing its LOOP. */
    E(HEADER_V2(0, 1) "anim 8 0 -1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 10,
      "animations[0].loop: unknown value -1 (expected 0..3)"),
    E(HEADER(0, 1) "anim 8 0 none 0 1:r\n", SPRAT_ERR_SYNTAX, 4, 10,
      "animations[0].alias_of: expected an integer, found \"none\""),
    E(HEADER(1, 1) SPRITE "anim 8 0 -1 1 1 1:r\n", SPRAT_ERR_SCHEMA, 5, 15,
      "animations[0].frames[0]: 1 is not a sprite index (the sheet has 1 sprite)"),
    E(HEADER(0, 1) "anim 8 0 -1 1 0 1:r\n", SPRAT_ERR_SCHEMA, 4, 15,
      "animations[0].frames[0]: 0 is not a sprite index (the sheet has 0 sprites)"),
    E(HEADER(1, 1) SPRITE "anim 8 0 -1 2 0 x 1:r\n" PAD, SPRAT_ERR_SYNTAX, 5, 17,
      "animations[0].frames[1]: expected an integer, found \"x\""),
    E(HEADER(0, 1) "anim 8 0 5 0 1:a\n", SPRAT_ERR_SCHEMA, 4, 10,
      "animations[0].alias_of: 5 is not an animation index (the sheet has 1 animation)"),
    E(HEADER(0, 2) "anim 8 0 -1 0 1:a\nanim 8 0 -2 0 1:b\n", SPRAT_ERR_SCHEMA, 5, 10,
      "animations[1].alias_of: -2 is not an animation index (the sheet has 2 animations)"),
    E(HEADER(0, 1) "anim 8 0 0 0 1:a\n", SPRAT_ERR_SCHEMA, 4, 10,
      "animations[0].alias_of: aliases form a cycle through animations[0] \"a\""),
    E(HEADER(0, 3) "anim 8 0 -1 0 1:a\nanim 8 0 2 0 1:b\nanim 8 0 1 0 1:c\n", SPRAT_ERR_SCHEMA,
      5, 10, "animations[1].alias_of: aliases form a cycle through animations[1] \"b\""),
    E(HEADER(1, 2) SPRITE "anim 8 0 -1 1 0 1:a\nanim 8 0 0 1 0 1:b\n", SPRAT_ERR_SCHEMA, 6, 12,
      "animations[1].frame_count: an alias has no frames of its own, found 1"),

    /* ---- Sprite aliases: checked once every sprite is loaded, so no position ---- */
    E(HEADER(2, 0) SPRITE SPRITE_AT("0 0 4 4", "0 1:b 5:b.png 5:x.png"), SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[1].alias_of: no sprite has the path \"x.png\""),
    E(HEADER(2, 0) SPRITE SPRITE_AT("4 0 4 4", "0 1:b 5:b.png 5:a.png"), SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[1].alias_of: \"a.png\" is in atlas 0 at 0,0 4x4 rotated 0, but this alias is in "
      "atlas 0 at 4,0 4x4 rotated 0"),
    E(HEADER(2, 0) SPRITE "sprite 0 0 0 4 4 90 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:b 5:b.png 5:a.png\n",
      SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[1].alias_of: \"a.png\" is in atlas 0 at 0,0 4x4 rotated 0, but this alias is in "
      "atlas 0 at 0,0 4x4 rotated 90"),
    /* An alias of an alias: only regular sprites are canonical. */
    E(HEADER(3, 0) SPRITE SPRITE_AT("0 0 4 4", "0 1:b 5:b.png 5:a.png")
          SPRITE_AT("0 0 4 4", "0 1:c 5:c.png 5:b.png"),
      SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[2].alias_of: \"b.png\" is sprites[1], itself an alias; an alias names a regular "
      "sprite"),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:a 5:a.png 5:a.png"), SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[0].alias_of: \"a.png\" is sprites[0], itself an alias; an alias names a regular "
      "sprite"),
    /* Paths are unique: the first repeat in the file is named, with the original. */
    E(HEADER(4, 0) SPRITE_AT("0 0 4 4", "0 1:a 3:b/a 0:") SPRITE_AT("0 0 4 4", "0 1:a 3:c/a 0:")
          SPRITE_AT("0 0 4 4", "0 1:x 3:c/a 0:") SPRITE_AT("0 0 4 4", "0 1:y 3:b/a 0:"),
      SPRAT_ERR_SCHEMA, 0, 0, "sprites[2].path: \"c/a\" is also the path of sprites[1]; paths are unique"),
    E("sprat-raw 1\nsheet 1 0 1 0 2 2 0 ~ 0: 0:\natlas 8 8 0: 0: 0:\natlas 8 8 0: 0: 0:\n"
      "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 5:a.png 0:\n"
      "sprite 1 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 5:a.png 0:\n",
      SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[1].path: \"a.png\" is also the path of sprites[0]; paths are unique"),
    /* Paths from the file are quoted with their control characters escaped,
     * terminal (C1) controls and bidirectional overrides included. */
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:a 5:a.png 9:\xC2\x9B" "2J\xE2\x80\xAE" "ab"),
      SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[0].alias_of: no sprite has the path \"\\u009B2J\\u202Eab\""),
    E(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 1:a 5:a.png 6:a\tb\"c\n"), SPRAT_ERR_SCHEMA, 0, 0,
      "sprites[0].alias_of: no sprite has the path \"a\\x09b\\\"c\\n\""),

    /* ---- The sheet line's totals ---- */
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 2 1 1 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 2, 1, "sheet.marker_total: declares 2 markers, the file has 1"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 2 1 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 2, 1, "sheet.vertex_total: declares 2 vertices, the file has 1"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 1 2 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 2, 1, "sheet.frame_total: declares 2 frames, the file has 1"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 1 1 5 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 2, 1, "sheet.string_byte_total: declares 5 string bytes, the file has 4"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 0 1 1 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 4, 46,
      "sprites[0].marker_count: 1 is more than the 0 left of the sheet line's marker_total (0)"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 0 1 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 5, 20,
      "sprites[0].markers[0].vertex_count: 1 is more than the 0 left of the sheet line's "
      "vertex_total (0)"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 1 0 4 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 6, 13,
      "animations[0].frame_count: 1 is more than the 0 left of the sheet line's frame_total (0)"),
    E("sprat-raw 1\nsheet 1 0 0 0 1 1 1 1 1 1 3 0: 0:\natlas 8 8 0: 0: 0:\n"
      SPRITE_AT("0 0 4 4", "1 1:a 1:a 0:") "marker 3 0 0 0 0 0 1 1 2 1:m\nanim 8 0 -1 1 0 1:r\n" PAD,
      SPRAT_ERR_SCHEMA, 6, 17,
      "animations[0].name: strings exceed the string byte total on the sheet line"),

    /* ---- After the last record ---- */
    E(HEADER(0, 0) "extra\n", SPRAT_ERR_SYNTAX, 4, 1,
      "unexpected data after the last record, found \"extra\""),
    E(HEADER(0, 0) "\n\n\t  anim 8 0 -1 0 1:r\n", SPRAT_ERR_SYNTAX, 6, 4,
      "unexpected data after the last record, found \"anim\""),
};

static void test_every_error(void) {
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
        const error_case* c = &k_cases[i];
        expect_error_at(__FILE__, c->source_line, c->text, c->status, c->line, c->column,
                        c->message);
    }
}

/* Embedded NUL bytes would silently truncate strings; the text needs its length. */
static void test_nul_in_string(void) {
    static const char text[] = "sprat-raw 1\nsheet 1 0 0 0 1 1 0 0 0 0 4 0: 0:\natlas 8 8 0: 0: 0:\n"
                               "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 3:a\0b 1:a 0:\n";
    sprat_sheet* sheet = NULL;
    sprat_error error;
    sprat_status status = sprat_sheet_load_memory(text, sizeof(text) - 1, &sheet, &error);
    check_error_at(__FILE__, __LINE__, status, &error, sheet, SPRAT_ERR_SYNTAX, 4, 51,
                   "sprites[0].name: strings must not contain NUL bytes, found \"\\x00b\"");
}

/* Lines end at \n (a \r before it is a space) and columns count code points. */
static void test_positions(void) {
    expect_error("sprat-raw 1\r\nsheet 1 0 0 0 1 0 0 0 0 0 1 0: 0:\r\natlas 8 8 1:a 0: 0:\r\n  x\r\n",
                 SPRAT_ERR_SYNTAX, 4, 3, "unexpected data after the last record, found \"x\"");
    expect_error(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 4:\xC3\xA9\xC3\xA9 1:a x"), SPRAT_ERR_SYNTAX,
                 4, 57, "sprites[0].alias_of: expected a string, found \"x\"");
    /* A string's newlines count as lines. */
    expect_error(HEADER(1, 0) SPRITE_AT("0 0 4 4", "0 3:a\nb 1:a 0:") "x\n", SPRAT_ERR_SYNTAX, 6, 1,
                 "unexpected data after the last record, found \"x\"");
}

/* Long tokens and values are cut in messages, which always end in a NUL. */
static void test_long_values_are_cut(void) {
    char text[512];
    char long_token[201];
    memset(long_token, 'z', 200);
    long_token[200] = '\0';
    snprintf(text, sizeof(text), "sprat-raw %s\n", long_token);
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(text, strlen(text), &sheet, &error), SPRAT_ERR_SYNTAX);
    CHECK_STR(error.message,
              "sprat-raw.version: expected an integer, found \"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz...\"");
    CHECK(memchr(error.message, '\0', sizeof(error.message)) != NULL);
}

/* ---- Sweeps over the fixture ---- */

/* What every failed load must look like, whatever the input. */
static void check_clean_failure(const char* what, size_t at, sprat_status status,
                                const sprat_error* error, const sprat_sheet* sheet) {
    bool ok = sheet == NULL && error->status == status && error->message[0] != '\0'
           && memchr(error->message, '\0', sizeof(error->message)) != NULL
           && (status == SPRAT_ERR_SYNTAX || status == SPRAT_ERR_SCHEMA)
           && (status != SPRAT_ERR_SYNTAX || error->line > 0);
    if (!ok) {
        FAIL_AT("%s at byte %zu: %s, line %zu column %zu: \"%s\"", what, at,
                sprat_status_string(status), error->line, error->column, error->message);
    }
}

/* A sheet with what the fixture lacks: polygons, circles, collision maps,
 * every slice mode, alias chains and empty strings. */
static const char k_everything[] =
    "sprat-raw 1\n"
    "sheet 2 3 1 0.5 2 3 3 4 3 4 55 3:n.p 3:c.p\n"
    "atlas 8 8 1:a 3:a.n 3:a.c\n"
    "atlas 16 4 1:b 0: 0:\n"
    "sprite 0 0 0 4 2 90 1 0 2 1 -3 5 1 1 1 2 2 2 3 3 4:poly 5:p.png 0:\n"
    "marker 3 0 0 0 0 0 3 0 0 4 0 2 -2 3:hit\n"
    "marker 1 1 2 3 0 0 0 4:ring\n"
    "marker 0 7 7 0 0 0 0 0:\n"
    "sprite 1 8 0 8 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0: 5:q.png 0:\n"
    "marker 2 0 0 0 8 4 0 3:box\n"
    "sprite 0 0 0 4 2 90 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:r 5:r.png 5:p.png\n"
    "anim 12 0 -1 4 0 1 2 1 4:walk\n"
    "anim 6 3 2 0 2:up\n"
    "anim 6 1 0 0 0:\n"
    /* Slack, so a shorter token cannot trip the "too short" check first. */
    "                                                  \n";

static void check_everything_loads(void) {
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK_INT(sprat_sheet_load_memory(k_everything, sizeof(k_everything) - 1, &sheet, &error),
              SPRAT_OK);
    if (sheet == NULL) fprintf(stderr, "  k_everything: %s\n", error.message);
    sprat_sheet_free(sheet);
}

/* Every cut fails, except one that only drops trailing whitespace, and none leaks. */
static void check_every_truncation(const char* label, const char* text, size_t size) {
    size_t content = size;
    /* The tokenizer's whitespace: a cut that drops only these still loads. */
    while (content > 0) {
        char c = text[content - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --content;
    }
    use_counting(0);
    for (size_t n = 0; n < size; ++n) {
        sprat_sheet* sheet = NULL;
        sprat_error error;
        sprat_status status = sprat_sheet_load_memory(text, n, &sheet, &error);
        if (n >= content) {
            if (status != SPRAT_OK) FAIL_AT("%s cut at byte %zu: %s", label, n, error.message);
            sprat_sheet_free(sheet);
        } else {
            check_clean_failure(label, n, status, &error, sheet);
            sprat_sheet_free(sheet); /* a wrong success is reported above, not as a leak */
        }
        if (g_counting.live != 0) FAIL_AT("%s cut at byte %zu leaked", label, n);
    }
    use_c_library();
}

static void test_every_truncation(void) {
    size_t size;
    char* text = read_fixture(&size);
    check_every_truncation("fixture", text, size);
    free(text);
    check_everything_loads();
    check_every_truncation("k_everything", k_everything, sizeof(k_everything) - 1);
}

/* The fields of each raw record in order, named as errors name them. "*" is
 * where a count's items go: vertices (two values each) or frames. */
static const char* const k_sheet_fields[] = {
    "sheet.scale", "sheet.extrude", "sheet.multipack", "sheet.collision_atlas_scale",
    "sheet.atlas_count", "sheet.sprite_count", "sheet.animation_count", "sheet.marker_total",
    "sheet.vertex_total", "sheet.frame_total", "sheet.string_byte_total",
    "sheet.normal_atlas_path", "sheet.collision_atlas_path", NULL,
};
static const char* const k_atlas_fields[] = {
    "width", "height", "path", "normal_path", "collision_path", NULL,
};
static const char* const k_sprite_fields[] = {
    "atlas_index", "rect.x", "rect.y", "rect.w", "rect.h", "rotation", "trim.left", "trim.top",
    "trim.right", "trim.bottom", "pivot.x", "pivot.y", "has_slice", "slice.left", "slice.top",
    "slice.right", "slice.bottom", "slice.h_mode", "slice.v_mode", "marker_count", "name", "path",
    "alias_of", NULL,
};
static const char* const k_marker_fields[] = {
    "type", "x", "y", "radius", "w", "h", "vertex_count", "*", "name", NULL,
};
static const char* const k_anim_fields[] = {
    "fps", "flip", "loop", "alias_of", "frame_count", "*", "name", NULL,
};
/* Version 1 anim records have no loop. */
static const char* const k_anim_fields_v1[] = {
    "fps", "flip", "alias_of", "frame_count", "*", "name", NULL,
};

typedef struct token {
    size_t start;
    size_t length;
    size_t line;
    size_t column;
    char name[64]; /* what an error about it names, then ": " */
} token;

/* Splits the fixture into tokens and names each one the way the loader's
 * errors do, from the record layout above rather than from the loader. */
static size_t name_tokens(const char* text, token* tokens, size_t capacity) {
    size_t count = 0;
    const char* p = text;
    const char* const* fields = NULL;
    size_t field = 0;
    char record[48] = "";
    size_t atlas = 0, sprite = 0, marker = 0, anim = 0;
    const char* const* anim_fields = k_anim_fields;
    size_t items = 0;     /* still to come for the current "*" */
    size_t item = 0;      /* index of the next one */
    bool vertex_y = false;
    size_t line = 1;
    const char* line_start = text;
    while (count < capacity) {
        size_t len;
        size_t ignored = 0;
        const char* before = p;
        const char* tok = next_token(&p, &len, &ignored);
        if (len == 0) break;
        for (const char* q = before; q < tok; ++q) {
            if (*q == '\n') {
                ++line;
                line_start = q + 1;
            }
        }
        token* t = &tokens[count++];
        t->start = (size_t)(tok - text);
        t->length = len;
        t->line = line;
        t->column = (size_t)(tok - line_start) + 1;
        if (strncmp(tok, "sprat-raw", len) == 0 && len == 9) {
            snprintf(t->name, sizeof(t->name), "not a sprat raw file: ");
            fields = NULL;
        } else if (fields == NULL && count == 2) {
            snprintf(t->name, sizeof(t->name), "sprat-raw.version: ");
            if (len == 1 && tok[0] == '1') anim_fields = k_anim_fields_v1;
        } else if (len == 5 && strncmp(tok, "sheet", 5) == 0) {
            snprintf(record, sizeof(record), "sheet");
            snprintf(t->name, sizeof(t->name), "sheet: ");
            fields = k_sheet_fields;
            field = 0;
        } else if (len == 5 && strncmp(tok, "atlas", 5) == 0) {
            snprintf(record, sizeof(record), "atlases[%zu]", atlas++);
            snprintf(t->name, sizeof(t->name), "%s: ", record);
            fields = k_atlas_fields;
            field = 0;
        } else if (len == 6 && strncmp(tok, "sprite", 6) == 0) {
            snprintf(record, sizeof(record), "sprites[%zu]", sprite++);
            snprintf(t->name, sizeof(t->name), "%s: ", record);
            fields = k_sprite_fields;
            field = 0;
            marker = 0;
        } else if (len == 6 && strncmp(tok, "marker", 6) == 0) {
            snprintf(record, sizeof(record), "sprites[%zu].markers[%zu]", sprite - 1, marker++);
            snprintf(t->name, sizeof(t->name), "%s: ", record);
            fields = k_marker_fields;
            field = 0;
        } else if (len == 4 && strncmp(tok, "anim", 4) == 0) {
            snprintf(record, sizeof(record), "animations[%zu]", anim++);
            snprintf(t->name, sizeof(t->name), "%s: ", record);
            fields = anim_fields;
            field = 0;
        } else if (fields != NULL && strcmp(fields[field], "*") == 0 && items > 0) {
            if (fields == k_marker_fields) {
                snprintf(t->name, sizeof(t->name), "%s.vertices[%zu].%s: ", record, item,
                         vertex_y ? "y" : "x");
                if (vertex_y) {
                    ++item;
                    --items;
                }
                vertex_y = !vertex_y;
            } else {
                snprintf(t->name, sizeof(t->name), "%s.frames[%zu]: ", record, item++);
                --items;
            }
            if (items == 0) ++field;
        } else if (fields != NULL) {
            if (strcmp(fields[field], "*") == 0) ++field; /* no items */
            if (fields == k_sheet_fields) {
                snprintf(t->name, sizeof(t->name), "%s: ", fields[field]);
            } else {
                snprintf(t->name, sizeof(t->name), "%s.%s: ", record, fields[field]);
            }
            bool counts_items = fields[field + 1] != NULL && strcmp(fields[field + 1], "*") == 0;
            if (counts_items) {
                items = (size_t)strtoul(tok, NULL, 10);
                item = 0;
                vertex_y = false;
            }
            ++field;
        }
    }
    return count;
}

/* Every token replaced with one that cannot be read there. The load must fail
 * at that token, and the error must name it. */
static void check_every_token_replaced(const char* label, const char* text, size_t size) {
    static token tokens[512];
    size_t count = name_tokens(text, tokens, 512);
    CHECK(count > 100);
    char* changed = malloc(size + 16);
    if (changed == NULL) abort();
    for (size_t i = 0; i < count; ++i) {
        const token* t = &tokens[i];
        /* "?" is not a tag, number, count or string. */
        memcpy(changed, text, t->start);
        changed[t->start] = '?';
        memcpy(changed + t->start + 1, text + t->start + t->length,
               size - t->start - t->length);
        size_t changed_size = size - t->length + 1;
        sprat_sheet* sheet = NULL;
        sprat_error error;
        sprat_status status = sprat_sheet_load_memory(changed, changed_size, &sheet, &error);
        bool ok = status == SPRAT_ERR_SYNTAX && sheet == NULL && error.line == t->line
               && error.column == t->column
               && strncmp(error.message, t->name, strlen(t->name)) == 0
               && strstr(error.message, ", found \"?\"") != NULL;
        if (!ok) {
            FAIL_AT("%s token %zu (%.*s at line %zu column %zu) replaced:\n"
                    "    got:      %s, line %zu column %zu: \"%s\"\n"
                    "    expected: syntax error, line %zu column %zu: \"%s...found \\\"?\\\"\"",
                    label, i, (int)t->length, text + t->start, t->line, t->column,
                    sprat_status_string(status), error.line, error.column, error.message,
                    t->line, t->column, t->name);
        }
        sprat_sheet_free(status == SPRAT_OK ? sheet : NULL);
    }
    free(changed);
}

static void test_every_token_replaced(void) {
    size_t size;
    char* text = read_fixture(&size);
    check_every_token_replaced("fixture", text, size);
    free(text);
    check_every_token_replaced("k_everything", k_everything, sizeof(k_everything) - 1);
}

/* A loaded sheet must keep every promise the header makes, so code using it
 * needs no checks of its own. */
static void check_sheet_invariants(const sprat_sheet* s) {
    CHECK(s->scale > 0);
    CHECK(s->atlas_count > 0);
    for (size_t i = 0; i < s->atlas_count; ++i) {
        const sprat_atlas* a = &s->atlases[i];
        CHECK(a->path != NULL && a->width > 0 && a->height > 0);
        CHECK(a->normal_path == NULL || a->normal_path[0] != '\0');
        CHECK(a->collision_path == NULL || a->collision_path[0] != '\0');
    }
    for (size_t i = 0; i < s->sprite_count; ++i) {
        const sprat_sprite* sp = &s->sprites[i];
        CHECK(sp->name != NULL && sp->path != NULL);
        CHECK(sp->atlas_index < s->atlas_count);
        const sprat_atlas* a = &s->atlases[sp->atlas_index];
        CHECK(sp->rect.x >= 0 && sp->rect.y >= 0 && sp->rect.w >= 0 && sp->rect.h >= 0);
        CHECK(sp->rect.x + sp->rect.w <= a->width && sp->rect.y + sp->rect.h <= a->height);
        CHECK(sp->rotation == 0 || sp->rotation == 90 || sp->rotation == 270);
        sprat_size source = sprat_sprite_source_size(sp);
        CHECK(source.w <= SPRAT_COORD_LIMIT && source.h <= SPRAT_COORD_LIMIT);
        if (sp->has_slice) {
            CHECK(sp->slice.left + sp->slice.right <= source.w);
            CHECK(sp->slice.top + sp->slice.bottom <= source.h);
        }
        if (sp->alias_of != NULL) {
            const sprat_sprite* c = NULL;
            for (size_t k = 0; k < s->sprite_count && c == NULL; ++k) {
                const sprat_sprite* o = &s->sprites[k];
                if (o->alias_of == NULL && strcmp(o->path, sp->alias_of) == 0
                    && o->atlas_index == sp->atlas_index && o->rotation == sp->rotation
                    && memcmp(&o->rect, &sp->rect, sizeof(sp->rect)) == 0) {
                    c = o;
                }
            }
            CHECK(c != NULL);
        }
        for (size_t k = 0; k < sp->marker_count; ++k) {
            const sprat_marker* m = &sp->markers[k];
            CHECK(m->name != NULL);
            CHECK((m->type == SPRAT_MARKER_POLYGON) == (m->vertex_count > 0));
        }
    }
    for (size_t i = 0; i < s->animation_count; ++i) {
        const sprat_animation* a = &s->animations[i];
        CHECK(a->name != NULL && a->fps >= 1 && a->flip <= 3);
        for (size_t k = 0; k < a->frame_count; ++k) CHECK(a->frames[k] < s->sprite_count);
    }
}

/* Seeded random corruption: flipped, deleted and duplicated bytes and digits.
 * Most loads fail and every failure is clean; the ones that load keep every
 * promise of the model. The sanitizer build catches any bad memory access. */
static void test_random_corruption(void) {
    size_t size;
    char* text = read_fixture(&size);
    char* changed = malloc(size * 2 + 16);
    if (changed == NULL) abort();
    unsigned long seed = 12345;
    static const char k_bytes[] = "0123456789 -:\n.e\xC3\xFF\x80";
    int loaded = 0;
    for (int iteration = 0; iteration < 20000; ++iteration) {
        memcpy(changed, text, size);
        size_t n = size;
        int edits = 1 + iteration % 4;
        for (int e = 0; e < edits; ++e) {
            seed = seed * 1103515245u + 12345u;
            size_t at = (seed >> 8) % n;
            unsigned kind = (unsigned)(seed >> 4) % 3;
            char byte = k_bytes[(seed >> 16) % (sizeof(k_bytes) - 1)];
            if (kind == 0) {
                changed[at] = byte;
            } else if (kind == 1 && n > 1) {
                memmove(changed + at, changed + at + 1, n - at - 1);
                --n;
            } else {
                memmove(changed + at + 1, changed + at, n - at);
                changed[at] = byte;
                ++n;
            }
        }
        sprat_sheet* sheet = NULL;
        sprat_error error;
        sprat_status status = sprat_sheet_load_memory(changed, n, &sheet, &error);
        if (status == SPRAT_OK) {
            ++loaded;
            check_sheet_invariants(sheet);
            sprat_sheet_free(sheet);
        } else {
            check_clean_failure("corrupted", (size_t)iteration, status, &error, sheet);
        }
    }
    CHECK(loaded > 0); /* some edits keep the file valid, such as a changed coordinate */
    free(changed);
    free(text);
}

int main(void) {
    RUN(test_every_error);
    RUN(test_nul_in_string);
    RUN(test_positions);
    RUN(test_long_values_are_cut);
    RUN(test_every_truncation);
    RUN(test_every_token_replaced);
    RUN(test_random_corruption);
    return TEST_RESULT();
}
