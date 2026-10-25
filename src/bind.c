/* sprat_sheet_bind(): resolves the names a program needs to indexes, all or
 * none, with messages that say what is wrong and how to fix it. */
#include "error.h"
#include "memory.h"
#include "reader.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KNOWN_FLAGS ((unsigned)SPRAT_BIND_OPTIONAL)
#define KIND_COUNT 3

/* A binding's result while resolving: an index, or one of these. */
#define MISSING SPRAT_NO_INDEX
#define AMBIGUOUS (SPRAT_NO_INDEX - 1)

static size_t entry_count(const sprat_sheet* sheet, sprat_bind_kind kind) {
    return kind == SPRAT_BIND_ANIMATION ? sheet->animation_count : sheet->sprite_count;
}

/* The string a binding of this kind is compared with. */
static const char* entry_key(const sprat_sheet* sheet, sprat_bind_kind kind, size_t i) {
    switch (kind) {
    case SPRAT_BIND_SPRITE: return sheet->sprites[i].name;
    case SPRAT_BIND_SPRITE_PATH: return sheet->sprites[i].path;
    case SPRAT_BIND_ANIMATION: return sheet->animations[i].name;
    }
    return "";
}

/* ---- Resolving ----------------------------------------------------------- */

/* Bottom-up merge sort of entry indexes by key; equal keys keep their order.
 * Returns whichever of items and scratch (count entries each) holds the result. */
static size_t* sort_entries(const sprat_sheet* sheet, sprat_bind_kind kind, size_t* items,
                            size_t* scratch, size_t count) {
    for (size_t width = 1; width < count; width *= 2) {
        for (size_t lo = 0; lo < count; lo += 2 * width) {
            size_t mid = count - lo > width ? lo + width : count;
            size_t hi = count - mid > width ? mid + width : count;
            size_t a = lo;
            size_t b = mid;
            size_t k = lo;
            while (a < mid && b < hi) {
                bool take_b =
                    strcmp(entry_key(sheet, kind, items[b]), entry_key(sheet, kind, items[a])) < 0;
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

/* The entry with that key, MISSING, or AMBIGUOUS when two or more have it. */
static size_t look_up(const sprat_sheet* sheet, sprat_bind_kind kind, const size_t* sorted,
                      size_t count, const char* name) {
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(entry_key(sheet, kind, sorted[mid]), name) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == count || strcmp(entry_key(sheet, kind, sorted[lo]), name) != 0) return MISSING;
    if (lo + 1 < count && strcmp(entry_key(sheet, kind, sorted[lo + 1]), name) == 0) {
        return AMBIGUOUS;
    }
    return sorted[lo];
}

static bool fails(const sprat_binding* b, size_t result) {
    return result == AMBIGUOUS || (result == MISSING && (b->flags & SPRAT_BIND_OPTIONAL) == 0);
}

/* ---- Messages: only once a binding fails, so plain scans do -------------- */

/* What a name matches: how many entries, and the first two. */
typedef struct match {
    size_t count;
    size_t first;
    size_t second;
} match;

static match find_all(const sprat_sheet* sheet, sprat_bind_kind kind, const char* name) {
    match m = {0, SPRAT_NO_INDEX, SPRAT_NO_INDEX};
    size_t n = entry_count(sheet, kind);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(entry_key(sheet, kind, i), name) != 0) continue;
        if (m.count == 0) {
            m.first = i;
        } else if (m.count == 1) {
            m.second = i;
        }
        ++m.count;
    }
    return m;
}

static bool has(const sprat_sheet* sheet, sprat_bind_kind kind, const char* name) {
    return find_all(sheet, kind, name).count > 0;
}

/* Text appended to a fixed buffer, cut at its end. */
typedef struct text {
    char* out;
    size_t size;
    size_t len;
} text;

static void append(text* t, const char* fmt, ...) {
    if (t->len + 1 >= t->size) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(t->out + t->len, t->size - t->len, fmt, args);
    va_end(args);
    if (n < 0) return;
    t->len += (size_t)n;
    if (t->len > t->size - 1) t->len = t->size - 1;
}

static size_t room(const text* t) {
    return t->size - 1 - t->len;
}

/* A name from the sheet or the caller, quoted and cut to `size` bytes. */
static void append_quoted(text* t, const char* s, size_t size) {
    char quoted[64];
    if (size > sizeof(quoted)) size = sizeof(quoted);
    sprat_quote(quoted, size, s, strlen(s));
    append(t, "%s", quoted);
}

/* " (the sheet has 2 animations: "run", "run-left")", with as many names as
 * fit, then "...". The closing parenthesis always fits: the list is left out
 * when not even its head and "...)" would. */
static void append_entries(text* t, const sprat_sheet* sheet, sprat_bind_kind kind) {
    const char* what = kind == SPRAT_BIND_ANIMATION ? "animation" : "sprite";
    size_t n = entry_count(sheet, kind);
    char head[64];
    if (n == 0) {
        snprintf(head, sizeof(head), " (the sheet has no %ss)", what);
        if (strlen(head) <= room(t)) append(t, "%s", head);
        return;
    }
    snprintf(head, sizeof(head), " (the sheet has %zu %s%s: ", n, what, n == 1 ? "" : "s");
    if (strlen(head) + 4 > room(t)) return;
    append(t, "%s", head);
    for (size_t i = 0; i < n; ++i) {
        const char* key = entry_key(sheet, kind, i);
        char quoted[44];
        sprat_quote(quoted, sizeof(quoted), key, strlen(key));
        const char* separator = i > 0 ? ", " : "";
        /* After this name: ", ..." when more follow, then ")". */
        size_t needed = strlen(separator) + strlen(quoted) + (i + 1 < n ? 5 : 0) + 1;
        if (needed > room(t)) {
            append(t, "%s...)", separator);
            return;
        }
        append(t, "%s%s", separator, quoted);
    }
    append(t, ")");
}

/* Why binding b fails, after its "bindings[i]: " prefix. */
static sprat_status describe(text* t, const sprat_sheet* sheet, const sprat_binding* b) {
    match m = find_all(sheet, b->kind, b->name);
    if (m.count > 1) {
        const char* more = m.count > 2 ? ", the first two" : "";
        if (b->kind == SPRAT_BIND_ANIMATION) {
            append(t, "%zu animations are named ", m.count);
            append_quoted(t, b->name, 48);
            append(t, "%s: animations[%zu] and animations[%zu]", more, m.first, m.second);
        } else {
            /* Paths are unique, so only names repeat. */
            append(t, "%zu sprites are named ", m.count);
            append_quoted(t, b->name, 40);
            append(t, "%s: sprites[%zu] ", more, m.first);
            append_quoted(t, sheet->sprites[m.first].path, 40);
            append(t, " and sprites[%zu] ", m.second);
            append_quoted(t, sheet->sprites[m.second].path, 40);
            append(t, "; bind one by its path with SPRAT_BIND_SPRITE_PATH");
        }
        return SPRAT_ERR_AMBIGUOUS;
    }

    switch (b->kind) {
    case SPRAT_BIND_SPRITE:
        append(t, "no sprite is named ");
        append_quoted(t, b->name, 48);
        if (has(sheet, SPRAT_BIND_SPRITE_PATH, b->name)) {
            append(t, ", but a sprite has that path: bind it with SPRAT_BIND_SPRITE_PATH");
        } else if (has(sheet, SPRAT_BIND_ANIMATION, b->name)) {
            append(t, ", but an animation is: bind it with SPRAT_BIND_ANIMATION");
        } else {
            append_entries(t, sheet, b->kind);
        }
        break;
    case SPRAT_BIND_SPRITE_PATH:
        append(t, "no sprite has the path ");
        append_quoted(t, b->name, 48);
        if (has(sheet, SPRAT_BIND_SPRITE, b->name)) {
            append(t, ", but a sprite has that name: bind it with SPRAT_BIND_SPRITE");
        } else {
            append_entries(t, sheet, b->kind);
        }
        break;
    case SPRAT_BIND_ANIMATION:
        append(t, "no animation is named ");
        append_quoted(t, b->name, 48);
        if (has(sheet, SPRAT_BIND_SPRITE, b->name)) {
            append(t, ", but a sprite is: bind it with SPRAT_BIND_SPRITE");
        } else {
            append_entries(t, sheet, b->kind);
        }
        break;
    }
    return SPRAT_ERR_NOT_FOUND;
}

/* ---- Binding ------------------------------------------------------------- */

static bool check_arguments(const sprat_sheet* sheet, const sprat_binding* bindings, size_t count,
                            sprat_error* error) {
    if (sheet == NULL) {
        sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "sheet must not be NULL");
        return false;
    }
    if (bindings == NULL && count > 0) {
        sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "bindings is NULL but count is %zu", count);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const sprat_binding* b = &bindings[i];
        if ((int)b->kind < 0 || (int)b->kind >= KIND_COUNT) {
            sprat_set_error(error, SPRAT_ERR_INVALID_ARG,
                            "bindings[%zu].kind is %d, which is not a sprat_bind_kind", i,
                            (int)b->kind);
            return false;
        }
        if (b->name == NULL) {
            sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "bindings[%zu].name must not be NULL", i);
            return false;
        }
        if (b->index == NULL) {
            sprat_set_error(error, SPRAT_ERR_INVALID_ARG, "bindings[%zu].index must not be NULL", i);
            return false;
        }
        if ((b->flags & ~KNOWN_FLAGS) != 0) {
            sprat_set_error(error, SPRAT_ERR_INVALID_ARG,
                            "bindings[%zu].flags has unknown bits 0x%X", i, b->flags & ~KNOWN_FLAGS);
            return false;
        }
    }
    return true;
}

/* Slots kept on the stack (4 KiB on 64-bit): one result per binding, then a
 * sorted index of one kind's entries and the sort's scratch. */
#define BIND_STACK_SLOTS 512

sprat_status sprat_sheet_bind(const sprat_sheet* sheet, const sprat_binding* bindings,
                              size_t count, sprat_error* error) {
    sprat_error local_error;
    if (error == NULL) {
        error = &local_error;
    }
    memset(error, 0, sizeof(*error));
    if (!check_arguments(sheet, bindings, count, error)) return error->status;
    if (count == 0) return SPRAT_OK;

    /* Sorting each kind that is bound makes every lookup a binary search:
     * O((entries + bindings) log entries), not entries times bindings. */
    bool used[KIND_COUNT] = {false, false, false};
    size_t largest = 0;
    for (size_t i = 0; i < count; ++i) {
        used[bindings[i].kind] = true;
        size_t n = entry_count(sheet, bindings[i].kind);
        if (n > largest) largest = n;
    }
    if (count > SIZE_MAX / sizeof(size_t) / 3 || largest > SIZE_MAX / sizeof(size_t) / 3) {
        sprat_set_error(error, SPRAT_ERR_NOMEM, "out of memory");
        return error->status;
    }
    size_t slot_count = count + 2 * largest;
    sprat_memory memory = sprat_memory_current();
    size_t stack_slots[BIND_STACK_SLOTS];
    size_t* slots = stack_slots;
    if (slot_count > BIND_STACK_SLOTS) {
        slots = memory.malloc_func(slot_count * sizeof(*slots));
        if (slots == NULL) {
            sprat_set_error(error, SPRAT_ERR_NOMEM, "out of memory");
            return error->status;
        }
    }
    size_t* results = slots;
    size_t* items = slots + count;
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        if (!used[kind]) continue;
        sprat_bind_kind k = (sprat_bind_kind)kind;
        size_t n = entry_count(sheet, k);
        for (size_t i = 0; i < n; ++i) items[i] = i;
        const size_t* sorted = sort_entries(sheet, k, items, items + n, n);
        for (size_t i = 0; i < count; ++i) {
            if (bindings[i].kind == k) results[i] = look_up(sheet, k, sorted, n, bindings[i].name);
        }
    }

    /* Every binding is resolved before any index is written, so a failure
     * leaves the caller's variables as they were. */
    size_t failing = 0;
    size_t first_failing = 0;
    for (size_t i = 0; i < count; ++i) {
        if (fails(&bindings[i], results[i])) {
            if (failing == 0) first_failing = i;
            ++failing;
        }
    }
    if (failing > 0) {
        text t = {error->message, sizeof(error->message), 0};
        if (failing == 1) {
            append(&t, "bindings[%zu]: ", first_failing);
        } else {
            append(&t, "bindings[%zu], the first of %zu that fail: ", first_failing, failing);
        }
        error->status = describe(&t, sheet, &bindings[first_failing]);
    } else {
        for (size_t i = 0; i < count; ++i) *bindings[i].index = results[i];
    }
    if (slots != stack_slots) memory.free_func(slots);
    return error->status;
}
