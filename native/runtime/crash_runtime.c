// crash_runtime.c - the OS boundary of the crash report
//
// The report itself (the error, its position, the call trace) is written by Core's crash_report in
// RazorForge, which reads the trace through the builder's trace helpers. Only the operating system's side
// stays here: writing bytes to the error stream, flushing, and exiting with the crash status.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/razorforge_runtime.h"
#include "types.h"

// Writes bytes to the error stream as they are, with no color or newline added.
void rf_console_error(const char* ptr, rf_address count)
{
    if (ptr != NULL && count > 0)
    {
        fwrite(ptr, 1, count, stderr);
    }
}

// Flushes buffered standard output, so a program's output appears before a crash report that follows it.
void rf_flush_output(void)
{
    fflush(stdout);
}

// Ends the program with the crash status after a crash report has been written.
void rf_crash_exit(void)
{
    fflush(stderr);
    exit(RF_EXIT_CRASH);
}
