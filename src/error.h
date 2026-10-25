/* Filling in a sprat_error, shared by the loader and the binder. */
#ifndef SPRAT_ERROR_H
#define SPRAT_ERROR_H

#include "sprat/sprat.h"

/* Sets status and a printf-style message, with no position. NULL is a no-op. */
void sprat_set_error(sprat_error* error, sprat_status status, const char* fmt, ...);

#endif /* SPRAT_ERROR_H */
