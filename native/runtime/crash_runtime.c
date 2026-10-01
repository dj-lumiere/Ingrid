// crash_runtime.c - Crash reporting with exe-registered shadow stack printer

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/razorforge_runtime.h"

#define RF_TRACE_MAX 32

typedef struct
{
    const char* routine;
    const char* file;
    int32_t line;
    int32_t col;
} rf_TraceFrame;

// Stack printer registered by the exe at startup (reads exe-local TLS shadow stack)
static void (*g_stack_printer)(void) = NULL;

void rf_set_stack_printer(void (*fn)(void))
{
    g_stack_printer = fn;
}

// Called by the exe's @_rf_print_trace_stack helper — prints the shadow stack
// data that lives in the exe's TLS globals.
void rf_print_shadow_stack_data(const rf_TraceFrame* stack, int32_t depth)
{
    if (depth <= 0) return;

    // Stack is a ring of RF_TRACE_MAX entries; depth may exceed RF_TRACE_MAX.
    // Print up to RF_TRACE_MAX most-recent frames, most recent first.
    int frames = depth < RF_TRACE_MAX ? (int)depth : RF_TRACE_MAX;
    fprintf(stderr, "Stack trace:\n");
    for (int i = 0; i < frames; i++)
    {
        // Most recent frame is at index (depth - 1) & (RF_TRACE_MAX - 1)
        int idx = (depth - 1 - i) & (RF_TRACE_MAX - 1);
        fprintf(stderr, "  %d: at %s (%s:%d:%d)\n",
                i,
                stack[idx].routine,
                stack[idx].file,
                stack[idx].line,
                stack[idx].col);
    }
    if (depth > RF_TRACE_MAX)
        fprintf(stderr, "  ... (%d frames total)\n", depth);
}

// Writes a UTF-32 codepoint buffer (a Text's data) to stderr as UTF-8.
static void rf_fput_utf32(const int32_t* text, int64_t count)
{
    if (text != NULL && count > 0)
    {
        for (int64_t i = 0; i < count; i++)
        {
            int32_t cp = text[i];
            if (cp <= 0x7F)
            {
                fputc(cp, stderr);
            }
            else if (cp <= 0x7FF)
            {
                fputc(0xC0 | (cp >> 6), stderr);
                fputc(0x80 | (cp & 0x3F), stderr);
            }
            else if (cp <= 0xFFFF)
            {
                fputc(0xE0 | (cp >> 12), stderr);
                fputc(0x80 | ((cp >> 6) & 0x3F), stderr);
                fputc(0x80 | (cp & 0x3F), stderr);
            }
            else
            {
                fputc(0xF0 | (cp >> 18), stderr);
                fputc(0x80 | ((cp >> 12) & 0x3F), stderr);
                fputc(0x80 | ((cp >> 6) & 0x3F), stderr);
                fputc(0x80 | (cp & 0x3F), stderr);
            }
        }
    }
}

void rf_crash(const char* type_name, int64_t type_len,
              const char* file, int64_t file_len,
              int32_t line, int32_t col,
              const int32_t* message_utf32, int64_t message_len)
{
    // Flush any buffered stdout first: rf_console_show no longer flushes per call when
    // stdout is piped, so partial program output could otherwise be lost or appear after
    // the crash report. (exit() below also flushes, but do it up front for ordering.)
    fflush(stdout);
    fprintf(stderr, "\033[91m%.*s: ", (int)type_len, type_name);
    rf_fput_utf32(message_utf32, message_len);

    fprintf(stderr, "\nat %.*s:%d:%d\n", (int)file_len, file, line, col);

    if (g_stack_printer)
        g_stack_printer();

    fprintf(stderr, "\033[0m");
    fflush(stderr);
    exit(RF_EXIT_CRASH);
}

// rf_crash with every text as a Text's UTF-32 data: the crash path the builder writes (`crash_report` in
// Core) passes the error type's name, the message and the file as Text values.
void rf_crash_text(const int32_t* type_name, int64_t type_len,
                   const int32_t* file, int64_t file_len,
                   int32_t line, int32_t col,
                   const int32_t* message, int64_t message_len)
{
    fflush(stdout);
    fprintf(stderr, "\033[91m");
    rf_fput_utf32(type_name, type_len);
    fprintf(stderr, ": ");
    rf_fput_utf32(message, message_len);
    fprintf(stderr, "\nat ");
    rf_fput_utf32(file, file_len);
    fprintf(stderr, ":%d:%d\n", line, col);

    if (g_stack_printer)
        g_stack_printer();

    fprintf(stderr, "\033[0m");
    fflush(stderr);
    exit(RF_EXIT_CRASH);
}
