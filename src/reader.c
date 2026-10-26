#include "reader.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool is_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static size_t utf8_sequence_length(const unsigned char* s, size_t n);

/* The code point of a well-formed sequence of len bytes. */
static unsigned decode_utf8(const unsigned char* s, size_t len) {
    unsigned cp = len == 1 ? s[0] : len == 2 ? s[0] & 0x1Fu : len == 3 ? s[0] & 0x0Fu : s[0] & 0x07u;
    for (size_t i = 1; i < len; ++i) cp = (cp << 6) | (s[i] & 0x3Fu);
    return cp;
}

/* Code points that change how a terminal or a text view shows what follows:
 * the C1 controls (U+009B is an escape sequence's start on some terminals)
 * and Unicode's Bidi_Control set, the marks and overrides of bidirectional text. */
static bool is_invisible_control(unsigned cp) {
    return (cp >= 0x80 && cp <= 0x9F) || cp == 0x061C || cp == 0x200E || cp == 0x200F
        || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
}

/* Errors are rare, so their line and column are counted only when one occurs. */
void sprat_reader_locate(const sprat_reader* r, size_t pos, size_t* line, size_t* column) {
    size_t l = 0;
    size_t c = 0;
    for (size_t i = 0; i < pos && i < r->size; ++i) {
        if (r->data[i] == '\n') {
            ++l;
            c = 0;
        } else if (((unsigned char)r->data[i] & 0xC0) != 0x80) {
            ++c; /* count code points, not UTF-8 continuation bytes */
        }
    }
    *line = l + 1;
    *column = c + 1;
}

/* Writes the escaped form of the character at text[i] to piece and returns its
 * length; *used is how many bytes of text it stands for. */
static size_t escape_piece(const char* text, size_t len, size_t i, bool escape_quote, char piece[12], size_t* used) {
    unsigned char c = (unsigned char)text[i];
    *used = 1;
    if ((c == '"' && escape_quote) || c == '\\') {
        piece[0] = '\\';
        piece[1] = (char)c;
        return 2;
    }
    if (c == '\n') {
        memcpy(piece, "\\n", 2);
        return 2;
    }
    if (c < 0x80 && c >= 0x20 && c != 0x7F) {
        piece[0] = (char)c;
        return 1;
    }
    size_t m = c < 0x80 ? 0 : utf8_sequence_length((const unsigned char*)text + i, len - i);
    if (m == 0) {
        snprintf(piece, 12, "\\x%02X", c); /* control or invalid byte */
        return 4;
    }
    *used = m;
    unsigned cp = decode_utf8((const unsigned char*)text + i, m);
    if (is_invisible_control(cp)) {
        snprintf(piece, 12, "\\u%04X", cp);
        return 6;
    }
    memcpy(piece, text + i, m);
    return m;
}

void sprat_quote(char* out, size_t size, const char* text, size_t len) {
    if (size < 8) {
        if (size > 0) out[0] = '\0';
        return;
    }
    /* Room after any piece for "...", the closing quote and the NUL. */
    const size_t limit = size - 5;
    size_t n = 0;
    out[n++] = '"';
    for (size_t i = 0; i < len;) {
        char piece[12];
        size_t used;
        size_t piece_len = escape_piece(text, len, i, true, piece, &used);
        if (n + piece_len > limit) {
            memcpy(out + n, "...", 3);
            n += 3;
            break;
        }
        memcpy(out + n, piece, piece_len);
        n += piece_len;
        i += used;
    }
    out[n++] = '"';
    out[n] = '\0';
}

size_t sprat_escape(char* out, size_t size, const char* text) {
    if (out == NULL) size = 0;
    size_t len = text == NULL ? 0 : strlen(text);
    size_t written = 0;
    size_t total = 0;
    for (size_t i = 0; i < len;) {
        char piece[12];
        size_t used;
        size_t piece_len = escape_piece(text, len, i, false, piece, &used);
        /* Once a piece doesn't fit, later ones are only counted. */
        if (written == total && piece_len < size - written) {
            memcpy(out + written, piece, piece_len);
            written += piece_len;
        }
        total += piece_len;
        i += used;
    }
    if (size > 0) out[written] = '\0';
    return total;
}

/* Fails with "<what>, found <the token at pos>", or "... found the end of the
 * file". The quote is cut at the token's end and at a few dozen bytes. */
static bool fail_at(sprat_reader* r, size_t pos, const char* what) {
    char found[48];
    if (pos >= r->size) {
        snprintf(found, sizeof(found), "the end of the file");
    } else {
        size_t end = pos;
        while (end < r->size && !is_space(r->data[end]) && end - pos < sizeof(found)) ++end;
        sprat_quote(found, sizeof(found), r->data + pos, end - pos);
    }
    r->error->status = SPRAT_ERR_SYNTAX;
    sprat_reader_locate(r, pos, &r->error->line, &r->error->column);
    snprintf(r->error->message, sizeof(r->error->message), "%s, found %s", what, found);
    return false;
}

static void skip_space(sprat_reader* r) {
    while (r->pos < r->size && is_space(r->data[r->pos])) {
        ++r->pos;
    }
    r->token_start = r->pos;
}

/* Moves past the token at r->pos and returns its length. */
static size_t token_length(sprat_reader* r) {
    size_t start = r->pos;
    while (r->pos < r->size && !is_space(r->data[r->pos])) {
        ++r->pos;
    }
    return r->pos - start;
}

bool sprat_reader_tag(sprat_reader* r, const char* tag) {
    skip_space(r);
    size_t start = r->pos;
    size_t len = token_length(r);
    if (len != strlen(tag) || memcmp(r->data + start, tag, len) != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected \"%s\"", tag);
        return fail_at(r, start, buf);
    }
    return true;
}

/* Reads [-]digits into *magnitude, stopping at the first non-digit. Digits past
 * 19 still count toward *extra_digits, exactly, so callers can scale or reject them. */
static bool read_digits(sprat_reader* r, uint64_t* magnitude, int* digits, size_t* extra_digits) {
    size_t start = r->pos;
    while (r->pos < r->size && is_digit(r->data[r->pos])) {
        if (*digits < 19) {
            *magnitude = *magnitude * 10 + (uint64_t)(r->data[r->pos] - '0');
            if (*magnitude != 0) ++*digits;
        } else {
            ++*extra_digits;
        }
        ++r->pos;
    }
    return r->pos > start;
}

static bool at_token_end(const sprat_reader* r) {
    return r->pos >= r->size || is_space(r->data[r->pos]);
}

bool sprat_reader_int(sprat_reader* r, int* out) {
    skip_space(r);
    size_t start = r->pos;
    bool negative = r->pos < r->size && r->data[r->pos] == '-';
    if (negative) ++r->pos;
    uint64_t magnitude = 0;
    int digits = 0;
    size_t extra = 0;
    if (!read_digits(r, &magnitude, &digits, &extra) || !at_token_end(r)) {
        return fail_at(r, start, "expected an integer");
    }
    if (extra > 0 || magnitude > (negative ? (uint64_t)INT_MAX + 1u : (uint64_t)INT_MAX)) {
        return fail_at(r, start, "integer out of range (-2147483648 to 2147483647)");
    }
    *out = negative ? (int)(-(int64_t)magnitude) : (int)magnitude;
    return true;
}

bool sprat_reader_count(sprat_reader* r, size_t* out) {
    skip_space(r);
    size_t start = r->pos;
    int value;
    if (!sprat_reader_int(r, &value)) return false;
    if (value < 0) return fail_at(r, start, "expected a count (0 or more)");
    /* Every item takes at least two bytes: a token and its separator. */
    if ((size_t)value > (r->size - r->pos) / 2) {
        return fail_at(r, start, "count is more than the rest of the file can hold");
    }
    *out = (size_t)value;
    return true;
}

/* Exact powers of ten representable as doubles. */
static const double k_pow10[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

/* Decimal to double without strtod, which depends on the C locale's decimal
 * separator. Exact whenever the significant digits fit in 2^53 and the decimal
 * exponent is within +-22; otherwise within a few ULPs. sprat-cli rounds values
 * to 8 significant digits before printing them with %.17g, and those 17-digit
 * tokens come out exact too (checked against strtod for 2 million of them). */
static double decimal_to_double(uint64_t mantissa, int exp10) {
    double value = (double)mantissa;
    if (mantissa == 0) {
        return 0.0;
    }
    if (exp10 >= 0) {
        while (exp10 > 22) {
            value *= 1e22;
            exp10 -= 22;
        }
        return value * k_pow10[exp10];
    }
    while (exp10 < -22) {
        value /= 1e22;
        exp10 += 22;
    }
    return value / k_pow10[-exp10];
}

bool sprat_reader_double(sprat_reader* r, double* out) {
    skip_space(r);
    size_t start = r->pos;
    bool negative = r->pos < r->size && r->data[r->pos] == '-';
    if (negative) ++r->pos;
    uint64_t mantissa = 0;
    int digits = 0;
    size_t dropped = 0;
    bool ok = read_digits(r, &mantissa, &digits, &dropped);
    /* Each term of exp10 is capped at 100000 (1e100000 overflows a double
     * anyway), so their sum cannot overflow int. */
    int exp10 = dropped > 100000 ? 100000 : (int)dropped;
    if (ok && r->pos < r->size && r->data[r->pos] == '.') {
        ++r->pos;
        size_t ignored = 0;
        size_t fraction_start = r->pos;
        ok = read_digits(r, &mantissa, &digits, &ignored);
        /* Every fraction digit that reached the mantissa, leading zeros included,
         * shifts the exponent; digits past the 19th were dropped. */
        size_t taken = (r->pos - fraction_start) - ignored;
        exp10 -= taken > 100000 ? 100000 : (int)taken;
    }
    if (ok && r->pos < r->size && (r->data[r->pos] == 'e' || r->data[r->pos] == 'E')) {
        ++r->pos;
        bool exp_negative = false;
        if (r->pos < r->size && (r->data[r->pos] == '+' || r->data[r->pos] == '-')) {
            exp_negative = r->data[r->pos] == '-';
            ++r->pos;
        }
        int e = 0;
        ok = r->pos < r->size && is_digit(r->data[r->pos]);
        while (r->pos < r->size && is_digit(r->data[r->pos])) {
            if (e < 100000) e = e * 10 + (r->data[r->pos] - '0');
            ++r->pos;
        }
        exp10 += exp_negative ? -e : e;
    }
    if (!ok || !at_token_end(r)) {
        return fail_at(r, start, "expected a number");
    }
    double value = decimal_to_double(mantissa, exp10);
    *out = negative ? -value : value;
    return true;
}

/* Length of the well-formed UTF-8 sequence at s (n bytes available), or 0.
 * Rejects overlong forms, surrogates and code points past U+10FFFF. */
static size_t utf8_sequence_length(const unsigned char* s, size_t n) {
    if (n == 0) return 0;
    unsigned char c = s[0];
    size_t len;
    unsigned cp;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1Fu; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0Fu; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07u; }
    else return 0;
    if (n < len) return 0;
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF
        || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return len;
}

bool sprat_reader_string(sprat_reader* r, const char** out) {
    skip_space(r);
    size_t start = r->pos;
    uint64_t len = 0;
    int digits = 0;
    size_t extra = 0;
    if (!read_digits(r, &len, &digits, &extra) || r->pos >= r->size || r->data[r->pos] != ':') {
        return fail_at(r, start, "expected a string");
    }
    ++r->pos;
    if (extra > 0 || len > r->size - r->pos) {
        return fail_at(r, start, "string runs past the end of the file");
    }
    size_t begin = r->pos;
    size_t end = begin + (size_t)len;
    if (end < r->size && !is_space(r->data[end])) {
        return fail_at(r, end, "expected a space after the string");
    }
    for (size_t i = begin; i < end;) {
        if (r->data[i] == '\0') {
            return fail_at(r, i, "strings must not contain NUL bytes");
        }
        size_t n = utf8_sequence_length((const unsigned char*)r->data + i, end - i);
        if (n == 0) {
            return fail_at(r, i, "invalid UTF-8 in string");
        }
        i += n;
    }
    size_t length = end - begin;
    if (length >= r->strings_left) {
        r->error->status = SPRAT_ERR_SCHEMA;
        sprat_reader_locate(r, start, &r->error->line, &r->error->column);
        snprintf(r->error->message, sizeof(r->error->message),
                 "strings exceed the string byte total on the sheet line");
        return false;
    }
    r->pos = end < r->size ? end + 1 : end;
    memcpy(r->strings, r->data + begin, length);
    r->strings[length] = '\0';
    *out = r->strings;
    r->strings += length + 1;
    r->strings_left -= length + 1;
    return true;
}

bool sprat_reader_end(sprat_reader* r) {
    skip_space(r);
    return r->pos >= r->size || fail_at(r, r->pos, "unexpected data after the last record");
}
