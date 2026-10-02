// runtime_init.c - Runtime process initialization

#ifdef _WIN32
#include <windows.h>
#endif

/* Start the I/O threads on the main thread (runtime-tessera/io.tess), so a coroutine's blocking call
 * is only a submit and a park. */
extern void rf_io_runtime_init(void);

void rf_runtime_init(void)
{
    rf_io_runtime_init();
#ifdef _WIN32
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    // Enable ANSI escape sequences for colored error output.
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode))
        {
            SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}
