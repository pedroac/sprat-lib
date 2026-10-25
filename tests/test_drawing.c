/* Checks the "Drawing a sprite" recipe in README.md. Each case packs an image
 * the way spratpack does, applies the recipe, draws the result the way raylib
 * and SDL3 do, and compares every screen pixel with the original image placed
 * at its pivot. Engine integrations live outside this library, so the engines
 * are modelled here rather than linked. */
#include "sprat/sprat.h"
#include "test.h"

#define ATLAS_SIZE 16
#define RECT_X 3
#define RECT_Y 4
#define SCREEN 128

typedef struct image_case {
    int w;
    int h;
    sprat_trim trim;
    sprat_point pivot;
} image_case;

/* A distinct, nonzero value for every pixel of the original image. */
static int original_pixel(int x, int y) {
    return y * 64 + x + 1;
}

static int content_w(const image_case* img) {
    return img->w - img->trim.left - img->trim.right;
}

static int content_h(const image_case* img) {
    return img->h - img->trim.top - img->trim.bottom;
}

/* Packs the trimmed image at RECT_X, RECT_Y, turned like spratpack's blit:
 *   CW (90):   atlas(col, row) <- content(row, h - 1 - col)
 *   CCW (270): atlas(col, row) <- content(w - 1 - row, col) */
static void pack(const image_case* img, int rotation, int atlas[ATLAS_SIZE][ATLAS_SIZE],
                 int* rect_w, int* rect_h) {
    int cw = content_w(img);
    int ch = content_h(img);
    *rect_w = rotation == 0 ? cw : ch;
    *rect_h = rotation == 0 ? ch : cw;
    memset(atlas, 0, sizeof(int) * ATLAS_SIZE * ATLAS_SIZE);
    for (int row = 0; row < *rect_h; ++row) {
        for (int col = 0; col < *rect_w; ++col) {
            int px = rotation == 90 ? row : rotation == 270 ? cw - 1 - row : col;
            int py = rotation == 90 ? ch - 1 - col : rotation == 270 ? col : row;
            atlas[RECT_Y + row][RECT_X + col] =
                original_pixel(img->trim.left + px, img->trim.top + py);
        }
    }
}

static sprat_sheet* load_sprite(const image_case* img, int rotation, int rect_w, int rect_h) {
    char text[256];
    int n = snprintf(text, sizeof(text),
                     "sprat-raw 1\nsheet 1 0 0 0 1 1 0 0 0 0 2 0: 0:\natlas %d %d 0: 0: 0:\n"
                     "sprite 0 %d %d %d %d %d %d %d %d %d %d %d 0 0 0 0 0 0 0 0 1:s 1:s 0:\n",
                     ATLAS_SIZE, ATLAS_SIZE, RECT_X, RECT_Y, rect_w, rect_h, rotation,
                     img->trim.left, img->trim.top, img->trim.right, img->trim.bottom,
                     img->pivot.x, img->pivot.y);
    sprat_sheet* sheet = NULL;
    sprat_error error;
    CHECK(n > 0 && (size_t)n < sizeof(text));
    if (sprat_sheet_load_memory(text, (size_t)n, &sheet, &error) != SPRAT_OK) {
        fprintf(stderr, "load failed: %s\n", error.message);
    }
    return sheet;
}

/* What the recipe hands to an engine. */
typedef struct draw_cmd {
    sprat_rect source;
    double center_x;
    double center_y;
    double w;
    double h;
    int angle; /* clockwise degrees */
    unsigned flip;
} draw_cmd;

/* The README recipe, line for line. */
static draw_cmd recipe(const sprat_sprite* s, double wx, double wy, double scale, unsigned flip) {
    sprat_size c = sprat_sprite_content_size(s);
    double dx = s->trim.left + c.w / 2.0 - s->pivot.x;
    double dy = s->trim.top + c.h / 2.0 - s->pivot.y;
    if (flip & SPRAT_FLIP_H) dx = -dx;
    if (flip & SPRAT_FLIP_V) dy = -dy;

    draw_cmd cmd;
    cmd.source = s->rect;
    cmd.w = s->rect.w * scale;
    cmd.h = s->rect.h * scale;
    cmd.center_x = wx + dx * scale;
    cmd.center_y = wy + dy * scale;
    cmd.angle = -s->rotation;
    cmd.flip = flip;
    if (s->rotation != 0) {
        cmd.flip = (unsigned)((flip & SPRAT_FLIP_H) ? SPRAT_FLIP_V : 0)
                 | (unsigned)((flip & SPRAT_FLIP_V) ? SPRAT_FLIP_H : 0);
    }
    return cmd;
}

/* The atlas value an engine shows at screen pixel (px, py), or 0 outside the
 * quad. Like DrawTexturePro and SDL_RenderTextureRotated, the source is flipped
 * inside the destination box, then the box is turned clockwise (Y down) around
 * its center. Angles are multiples of 90, so the arithmetic is exact. */
static int engine_pixel(int atlas[ATLAS_SIZE][ATLAS_SIZE], const draw_cmd* c, int px, int py) {
    double x = px + 0.5 - c->center_x;
    double y = py + 0.5 - c->center_y;
    int undo = ((-c->angle) % 360 + 360) % 360;
    double cs = undo == 0 ? 1 : undo == 180 ? -1 : 0;
    double sn = undo == 90 ? 1 : undo == 270 ? -1 : 0;
    double u = x * cs - y * sn + c->w / 2;
    double v = x * sn + y * cs + c->h / 2;
    if (u < 0 || v < 0 || u >= c->w || v >= c->h) return 0;
    if (c->flip & SPRAT_FLIP_H) u = c->w - u;
    if (c->flip & SPRAT_FLIP_V) v = c->h - v;
    int sx = (int)(u / c->w * c->source.w);
    int sy = (int)(v / c->h * c->source.h);
    if (sx >= c->source.w) sx = c->source.w - 1;
    if (sy >= c->source.h) sy = c->source.h - 1;
    return atlas[c->source.y + sy][c->source.x + sx];
}

/* The original pixel that belongs at screen pixel (px, py) when the image is
 * drawn with its pivot at (wx, wy) and flipped about the pivot, or 0 where the
 * trim removed it. */
static int expected_pixel(const image_case* img, double wx, double wy, double scale,
                          unsigned flip, int px, int py) {
    double x = (px + 0.5 - wx) / scale;
    double y = (py + 0.5 - wy) / scale;
    if (flip & SPRAT_FLIP_H) x = -x;
    if (flip & SPRAT_FLIP_V) y = -y;
    x += img->pivot.x;
    y += img->pivot.y;
    if (x < img->trim.left || x >= img->w - img->trim.right ||
        y < img->trim.top || y >= img->h - img->trim.bottom) {
        return 0;
    }
    return original_pixel((int)x, (int)y);
}

static void test_recipe_matches_original(void) {
    static const image_case images[] = {
        {9, 6, {1, 2, 3, 1}, {6, 2}},  /* odd content size: centers on half pixels */
        {9, 6, {1, 2, 3, 1}, {0, 0}},  /* no pivot marker */
        {10, 6, {1, 2, 3, 0}, {5, 3}}, /* the README example */
    };
    static const int rotations[] = {0, 90, 270};
    static const double scales[] = {1, 2};
    int atlas[ATLAS_SIZE][ATLAS_SIZE];

    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        const image_case* img = &images[i];
        for (size_t r = 0; r < sizeof(rotations) / sizeof(rotations[0]); ++r) {
            int rect_w = 0;
            int rect_h = 0;
            pack(img, rotations[r], atlas, &rect_w, &rect_h);
            sprat_sheet* sheet = load_sprite(img, rotations[r], rect_w, rect_h);
            CHECK(sheet != NULL);
            if (sheet == NULL) continue;
            for (unsigned flip = 0; flip < 4; ++flip) {
                for (size_t k = 0; k < sizeof(scales) / sizeof(scales[0]); ++k) {
                    double scale = scales[k];
                    draw_cmd cmd = recipe(&sheet->sprites[0], 40, 40, scale, flip);
                    int mismatches = 0;
                    int drawn = 0;
                    for (int py = 0; py < SCREEN; ++py) {
                        for (int px = 0; px < SCREEN; ++px) {
                            int got = engine_pixel(atlas, &cmd, px, py);
                            if (got != expected_pixel(img, 40, 40, scale, flip, px, py)) {
                                ++mismatches;
                            }
                            if (got != 0) ++drawn;
                        }
                    }
                    int area = (int)(content_w(img) * content_h(img) * scale * scale);
                    if (mismatches != 0 || drawn != area) {
                        fprintf(stderr, "image %zu, rotation %d, flip %u, scale %g: "
                                        "%d mismatched pixels, %d of %d drawn\n",
                                i, rotations[r], flip, scale, mismatches, drawn, area);
                    }
                    CHECK(mismatches == 0);
                    CHECK(drawn == area);
                }
            }
            sprat_sheet_free(sheet);
        }
    }
}

/* The numbers quoted in the README's worked example. */
static void test_readme_example(void) {
    const image_case img = {10, 6, {1, 2, 3, 0}, {5, 3}};
    int atlas[ATLAS_SIZE][ATLAS_SIZE];
    int rect_w = 0;
    int rect_h = 0;
    pack(&img, 90, atlas, &rect_w, &rect_h);
    CHECK(rect_w == 4 && rect_h == 6);
    sprat_sheet* sheet = load_sprite(&img, 90, rect_w, rect_h);
    CHECK(sheet != NULL);
    if (sheet == NULL) return;

    draw_cmd cmd = recipe(&sheet->sprites[0], 100, 50, 1, 0);
    CHECK(cmd.w == 4 && cmd.h == 6);
    CHECK(cmd.center_x == 99 && cmd.center_y == 51);
    CHECK(cmd.angle == -90);

    int min_x = SCREEN, min_y = SCREEN, max_x = -1, max_y = -1;
    for (int py = 0; py < SCREEN; ++py) {
        for (int px = 0; px < SCREEN; ++px) {
            if (engine_pixel(atlas, &cmd, px, py) == 0) continue;
            if (px < min_x) min_x = px;
            if (py < min_y) min_y = py;
            if (px > max_x) max_x = px;
            if (py > max_y) max_y = py;
        }
    }
    /* "covers x 96..102 and y 49..53": pixel spans, so the last pixels are 101 and 52. */
    CHECK(min_x == 96 && max_x == 101 && min_y == 49 && max_y == 52);
    CHECK(engine_pixel(atlas, &cmd, 96, 49) == original_pixel(1, 2));
    sprat_sheet_free(sheet);
}

/* README: SPRAT_FLIP_* can be cast to SDL_FlipMode, where SDL_FLIP_HORIZONTAL
 * is 1, SDL_FLIP_VERTICAL is 2 and SDL_FLIP_HORIZONTAL_AND_VERTICAL is 3. */
static void test_flip_bits_match_sdl(void) {
    CHECK(SPRAT_FLIP_H == 1);
    CHECK(SPRAT_FLIP_V == 2);
    CHECK((SPRAT_FLIP_H | SPRAT_FLIP_V) == 3);
}

int main(void) {
    RUN(test_recipe_matches_original);
    RUN(test_readme_example);
    RUN(test_flip_bits_match_sdl);
    return TEST_RESULT();
}
