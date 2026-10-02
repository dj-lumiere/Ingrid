#include "types.h"
#include "../include/razorforge_runtime.h"

#include <stdlib.h>

/* Runtime error + stack trace + exit(RF_EXIT_CRASH) (stacktrace.c). */
extern void __rf_throw(const char* error_type, const char* message);

struct rf_context_runtime
{
    rf_U64 reserved;
};

/* The coroutine switch is Ingrid's Tessera runtime code (runtime-tessera/coroutine.tess), on every
 * operating system. */
const char* rf_context_backend_name(void)
{
    return "tessera";
}

rf_runtime_backend_state rf_context_backend_state(void)
{
    return RF_RUNTIME_BACKEND_AVAILABLE;
}

rf_context_runtime* rf_context_runtime_create(void)
{
    rf_context_runtime* runtime = (rf_context_runtime*)calloc(1, sizeof(rf_context_runtime));
    if (runtime == NULL) {
        __rf_throw("OutOfMemoryError", "Failed to allocate context runtime");
        return NULL; /* unreachable */
    }
    return runtime;
}

void rf_context_runtime_destroy(rf_context_runtime* runtime)
{
    free(runtime);
}

int rf_context_runtime_spawn(rf_context_runtime* runtime, rf_context_entry_fn entry, void* userdata, size_t stack_size)
{
    (void)runtime;
    (void)stack_size;

    /* Coroutines go through coro_runtime.c (rf_coro_create / rf_sched_spawn); this older entry point
     * runs the routine to completion on the calling thread. */
    if (entry == NULL) return 0;
    entry(userdata);
    return 1;
}
