/* sprat-dump: prints what sprat-lib loads from a spratconvert raw file.
 *
 *   spratconvert --transform raw < layout.txt > sheet.sprat
 *   sprat-dump sheet.sprat
 */
#include <sprat/sprat.h>

#include <stdio.h>

/* Names and paths come from the file, so control characters are escaped rather
 * than handed to the terminal. Strings are valid UTF-8, so the only multi-byte
 * controls are C1 (U+0080..U+009F, encoded C2 80..C2 9F). */
static void print_text(const char* text) {
    const unsigned char* s = (const unsigned char*)text;
    while (*s != '\0') {
        if (*s < 0x20 || *s == 0x7F) {
            printf("\\x%02X", *s);
            s += 1;
        } else if (*s == 0xC2 && s[1] >= 0x80 && s[1] <= 0x9F) {
            printf("\\u%04X", s[1]);
            s += 2;
        } else {
            putchar(*s);
            s += 1;
        }
    }
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

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SHEET.sprat\n", argv[0]);
        return 2;
    }

    sprat_sheet* sheet = NULL;
    sprat_error error;
    if (sprat_sheet_load_file(argv[1], &sheet, &error) != SPRAT_OK) {
        if (error.line != 0) {
            fprintf(stderr, "%s:%zu:%zu: %s\n", argv[1], error.line, error.column, error.message);
        } else {
            fprintf(stderr, "%s: %s: %s\n", argv[1], sprat_status_string(error.status), error.message);
        }
        return 1;
    }

    printf("scale %g, extrude %d%s\n", sheet->scale, sheet->extrude,
           sheet->multipack ? ", multipack" : "");
    for (size_t i = 0; i < sheet->atlas_count; ++i) {
        const sprat_atlas* a = &sheet->atlases[i];
        printf("atlas %zu: ", i);
        print_text(a->path);
        printf(" %dx%d\n", a->width, a->height);
    }
    for (size_t i = 0; i < sheet->sprite_count; ++i) {
        const sprat_sprite* s = &sheet->sprites[i];
        sprat_size source = sprat_sprite_source_size(s);
        printf("sprite %zu: ", i);
        print_text(s->name);
        printf(" [atlas %zu] %d,%d %dx%d source %dx%d pivot %d,%d", s->atlas_index, s->rect.x, s->rect.y, s->rect.w, s->rect.h,
               source.w, source.h, s->pivot.x, s->pivot.y);
        if (s->rotation != 0) printf(" rotated %d", s->rotation);
        if (s->alias_of != NULL) {
            printf(" alias of ");
            print_text(s->alias_of);
        }
        if (s->has_slice) {
            printf(" slice %d,%d,%d,%d", s->slice.left, s->slice.top, s->slice.right, s->slice.bottom);
        }
        printf("\n");
        for (size_t k = 0; k < s->marker_count; ++k) {
            const sprat_marker* m = &s->markers[k];
            printf("  marker ");
            print_text(m->name);
            printf(" %s %d,%d\n", marker_type_name(m->type), m->x, m->y);
        }
    }
    for (size_t i = 0; i < sheet->animation_count; ++i) {
        const sprat_animation* a = &sheet->animations[i];
        printf("animation ");
        print_text(a->name);
        printf(": %d fps, %zu frames", a->fps, a->frame_count);
        if (a->alias_of != NULL) {
            printf(" (alias of ");
            print_text(a->alias_of);
            printf(")");
        }
        printf("\n");
    }

    sprat_sheet_free(sheet);
    return 0;
}
