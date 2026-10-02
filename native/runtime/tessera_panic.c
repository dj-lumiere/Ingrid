/*
 * tessera_panic.c — the panic handler of Ingrid's code written in Tessera.
 *
 * Tessera code (the .tess files of runtime-tessera, in this DLL, and those of Ingrid/tessera, linked into every
 * RazorForge and Suflae module) ends a broken contract by calling tessera_panic_handler: the reason,
 * an optional message, and the place of the call (Tessera's #track_caller). Here it becomes a
 * RazorForge runtime error, with the same crash report and exit status as any other.
 */

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

extern void __rf_throw(const char* error_type, const char* message);

/* Tessera's Bytes and SourceLocation, as the C ABI passes them. */
typedef struct {
    const char* data;
    size_t length;
} tessera_bytes;

typedef struct {
    tessera_bytes file;
    uint32_t line;
    uint32_t column;
} tessera_source_location;

/* Tessera's TrapCode, in its order (stdlib/panic.tess). */
static const char* tessera_trap_description(uint8_t code)
{
    switch (code) {
        case 0:  return "internal error: entered unreachable code";
        case 1:  return "index out of bounds";
        case 2:  return "the collection is empty";
        case 3:  return "key not found";
        case 4:  return "unwrap of Absent";
        case 5:  return "unwrap of a Failure";
        case 6:  return "unwrap_failure of a Success";
        case 7:  return "memory allocation failed";
        case 8:  return "invalid argument";
        case 9:  return "assertion failed";
        case 10: return "arithmetic overflow";
        case 11: return "division by zero";
        case 12: return "the operating system didn't start a thread";
        default: return "error code";
    }
}

void tessera_panic_handler(uint8_t code, tessera_bytes message, const tessera_source_location* place)
{
    char text[1024];
    int n = snprintf(text, sizeof text, "%s", tessera_trap_description(code));
    if (message.length > 0 && n >= 0 && (size_t)n < sizeof text) {
        n += snprintf(text + n, sizeof text - (size_t)n, ": %.*s", (int)message.length, message.data);
    }
    if (place != NULL && n >= 0 && (size_t)n < sizeof text) {
        if (place->column > 0) {
            snprintf(text + n, sizeof text - (size_t)n, " (at %.*s:%u:%u)", (int)place->file.length,
                     place->file.data, place->line, place->column);
        } else {
            snprintf(text + n, sizeof text - (size_t)n, " (at %.*s:%u)", (int)place->file.length,
                     place->file.data, place->line);
        }
    }
    __rf_throw("TesseraPanic", text);
}
