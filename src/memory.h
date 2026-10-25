/* The memory functions set with sprat_set_memory_functions(). */
#ifndef SPRAT_MEMORY_H
#define SPRAT_MEMORY_H

#include "sprat/sprat.h"

/* The memory functions in effect when a load starts. A load takes one snapshot
 * and uses it throughout, and a sheet keeps its free function so
 * sprat_sheet_free() releases the sheet with the functions that allocated it. */
typedef struct sprat_memory {
    sprat_malloc_func malloc_func;
    sprat_calloc_func calloc_func;
    sprat_realloc_func realloc_func;
    sprat_free_func free_func;
} sprat_memory;

sprat_memory sprat_memory_current(void);

/* Stand-in for C11 max_align_t, which MSVC's C mode lacks: the alignment that
 * malloc guarantees and every section of a sheet's block starts on. */
typedef union sprat_max_align {
    long double ld;
    long long ll;
    void* p;
    void (*fp)(void);
} sprat_max_align;

#define SPRAT_ALIGN (sizeof(sprat_max_align))

#endif /* SPRAT_MEMORY_H */
