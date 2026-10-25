#include "memory.h"

#include <stdlib.h>

/* Wrappers rather than the C functions themselves: with a DLL C runtime, MSVC's
 * C mode does not accept their addresses in a static initializer. */
static void* default_malloc(size_t size) { return malloc(size); }
static void* default_calloc(size_t count, size_t size) { return calloc(count, size); }
static void* default_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
static void default_free(void* ptr) { free(ptr); }

/* Not synchronized: set before loading sheets, as with SDL_SetMemoryFunctions. */
static sprat_memory g_memory = {default_malloc, default_calloc, default_realloc, default_free};

bool sprat_set_memory_functions(sprat_malloc_func malloc_func, sprat_calloc_func calloc_func,
                                sprat_realloc_func realloc_func, sprat_free_func free_func) {
    if (malloc_func == NULL || calloc_func == NULL || realloc_func == NULL || free_func == NULL) {
        return false;
    }
    g_memory.malloc_func = malloc_func;
    g_memory.calloc_func = calloc_func;
    g_memory.realloc_func = realloc_func;
    g_memory.free_func = free_func;
    return true;
}

void sprat_get_memory_functions(sprat_malloc_func* malloc_func, sprat_calloc_func* calloc_func,
                                sprat_realloc_func* realloc_func, sprat_free_func* free_func) {
    if (malloc_func != NULL) *malloc_func = g_memory.malloc_func;
    if (calloc_func != NULL) *calloc_func = g_memory.calloc_func;
    if (realloc_func != NULL) *realloc_func = g_memory.realloc_func;
    if (free_func != NULL) *free_func = g_memory.free_func;
}

sprat_memory sprat_memory_current(void) {
    return g_memory;
}
