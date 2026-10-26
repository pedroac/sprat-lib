/* sprat-dump: prints what sprat-lib loads from a spratconvert raw file.
 *
 *   spratconvert --transform raw < layout.txt > sheet.sprat
 *   sprat-dump sheet.sprat
 */
#include <sprat/sprat.h>

#include <stdio.h>
#include <stdlib.h>

/* Names and paths come from the file, so they are escaped rather than handed
 * to the terminal as is. */
static void print_text(const char* text) {
    size_t len = sprat_escape(NULL, 0, text);
    char* escaped = malloc(len + 1);
    if (escaped == NULL) return;
    sprat_escape(escaped, len + 1, text);
    fputs(escaped, stdout);
    free(escaped);
}

static const char* loop_name(unsigned loop) {
    switch (loop) {
    case SPRAT_LOOP_ONCE: return "once";
    case SPRAT_LOOP_PINGPONG: return "pingpong";
    case SPRAT_LOOP_ONCE | SPRAT_LOOP_PINGPONG: return "once-pingpong";
    default: return "loop";
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
        printf(": %d fps, %zu frames, %s", a->fps, a->frame_count, loop_name(a->loop));
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
