#include "error.h"

#include <stdarg.h>
#include <stdio.h>

void sprat_set_error(sprat_error* error, sprat_status status, const char* fmt, ...) {
    if (error == NULL) {
        return;
    }
    error->status = status;
    error->line = 0;
    error->column = 0;
    va_list args;
    va_start(args, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
}
