/* Tokenizer for spratconvert's `raw` format: space-separated tokens, with
 * strings written as "<byte length>:<bytes>". See transforms/raw.jsonnet in sprat-cli. */
#ifndef SPRAT_READER_H
#define SPRAT_READER_H

#include "sprat/sprat.h"

typedef struct sprat_reader {
    const char* data; /* the input; never modified, need not be NUL-terminated */
    size_t size;
    size_t pos;
    size_t token_start; /* where the last token read (or failed to read) starts */
    sprat_error* error;
    /* Pool that receives a NUL-terminated copy of every string read, empty
     * ones included, so its size is exactly the string bytes plus one per string. */
    char* strings;
    size_t strings_left;
} sprat_reader;

/* Each reader skips leading whitespace, reads one token and fails with
 * SPRAT_ERR_SYNTAX (line and column set) when the token does not fit. The
 * message says what was expected and quotes what was found. */
bool sprat_reader_tag(sprat_reader* r, const char* tag);
bool sprat_reader_int(sprat_reader* r, int* out);
bool sprat_reader_double(sprat_reader* r, double* out);
/* A count of items still to come; rejected when the rest of the input is too
 * short to hold that many, so bad counts cannot request huge allocations. */
bool sprat_reader_count(sprat_reader* r, size_t* out);
/* A NUL-terminated UTF-8 copy in r->strings. Fails with SPRAT_ERR_SCHEMA when
 * the pool has no room left: the file declared fewer string bytes than it has. */
bool sprat_reader_string(sprat_reader* r, const char** out);
/* Succeeds when only whitespace is left. */
bool sprat_reader_end(sprat_reader* r);

/* The 1-based line and column of byte pos, counting UTF-8 code points. */
void sprat_reader_locate(const sprat_reader* r, size_t pos, size_t* line, size_t* column);

/* Writes up to `size` bytes (NUL included) of a quoted copy of text[0..len)
 * for an error message, safe to print: '"', '\\', control characters and
 * invalid bytes escaped as sprat_escape() does, and "..." when cut short. */
void sprat_quote(char* out, size_t size, const char* text, size_t len);

#endif /* SPRAT_READER_H */
