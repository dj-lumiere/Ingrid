#include "types.h"
#include "../include/razorforge_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rf_sync.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h> /* _beginthreadex */
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Runtime error + stack trace + exit(RF_EXIT_CRASH) (stacktrace.c). */
extern void __rf_throw(const char* error_type, const char* message);

struct rf_async_runtime
{
    rf_Bool should_stop;
};

/* Blocking I/O runs on the runtime's own I/O threads (below), on every operating system. */
const char* rf_async_backend_name(void)
{
    return "threads";
}

rf_runtime_backend_state rf_async_backend_state(void)
{
    return RF_RUNTIME_BACKEND_AVAILABLE;
}

rf_async_runtime* rf_async_runtime_create(void)
{
    rf_async_runtime* runtime = (rf_async_runtime*)calloc(1, sizeof(rf_async_runtime));
    if (runtime == NULL) {
        __rf_throw("OutOfMemoryError", "Failed to allocate async runtime");
        return NULL; /* unreachable */
    }
    return runtime;
}

void rf_async_runtime_destroy(rf_async_runtime* runtime)
{
    free(runtime);
}

int rf_async_runtime_run_once(rf_async_runtime* runtime)
{
    if (runtime == NULL) return 0;
    return runtime->should_stop ? 0 : 1;
}

int rf_async_runtime_run_default(rf_async_runtime* runtime)
{
    if (runtime == NULL) return 0;
    while (!runtime->should_stop)
    {
        if (!rf_async_runtime_run_once(runtime))
        {
            break;
        }
    }
    return 1;
}

void rf_async_runtime_stop(rf_async_runtime* runtime)
{
    if (runtime == NULL) return;
    runtime->should_stop = true;
}

/* ------------------------------------------------------------------------------------------------
 * Coroutine I/O parking: the runtime's own I/O threads.
 *
 * RF_IO_THREADS threads, started once at runtime init, take blocking work (reading or writing a whole
 * file, running a subprocess) from a queue. A scheduler-driven coroutine that wants to do blocking
 * I/O submits a request, parks itself off the scheduler (rf_sched_park_external), and the I/O thread
 * that finishes the work wakes it through the cross-thread wake (rf_sched_wake), so the coroutine's
 * worker goes on with other coroutines meanwhile. Outside a coroutine the work runs on the calling
 * thread. The same on every operating system: the work itself is the plain blocking call, on a thread
 * of its own. (This replaced a libuv loop thread + libuv's thread pool.)
 * ------------------------------------------------------------------------------------------------ */

/* Exported by coro_runtime.c — the scheduler / coroutine accessors + the cross-thread wake. */
extern rf_sched* rf_sched_current(void);
extern rf_coro*  rf_coro_current(void);
extern void      rf_sched_park_external(void);
extern void      rf_sched_wake(rf_sched* sched, rf_coro* coro);
extern void      rf_sched_arm_cross_waker(rf_sched* sched);
extern void      rf_sched_disarm_cross_waker(rf_sched* sched);

#define RF_IO_THREADS 4

typedef enum rf_io_kind {
    RF_IO_READ_FILE = 0,
    RF_IO_WRITE_FILE = 1,
    RF_IO_RUN_PROCESS = 2
} rf_io_kind;

struct rf_proc_state; /* defined below; non-NULL only for RF_IO_RUN_PROCESS requests */

typedef struct rf_io_req {
    rf_io_kind kind;
    rf_sched* sched;           /* scheduler driving the awaiting coroutine                      */
    rf_coro* coro;             /* the parked coroutine to wake on completion                    */
    struct rf_io_req* qnext;   /* submission-queue link                                         */

    char* path;                /* malloc'd NUL-terminated copy of the path                      */
    char* data;                /* read: malloc'd file contents (caller frees). write: malloc'd  */
                               /*       copy of the payload (freed by rf_io_write_file_all).     */
    int64_t data_len;          /* write: number of payload bytes in `data`                      */
    int64_t result;            /* read: bytes read. write: bytes written. < 0 on error          */
    struct rf_proc_state* proc; /* RF_IO_RUN_PROCESS: the command and its captured output        */
} rf_io_req;

/* The I/O threads' queue (FIFO). */
static rf_mutex g_io_lock;
static rf_cond g_io_work;      /* signalled when a request is queued */
static rf_io_req* g_io_head = NULL;
static rf_io_req* g_io_tail = NULL;
static int g_io_ok = 0;        /* 1 once the I/O threads run; until then everything runs inline */

/* Per-result length, mirroring rf_get_result_len for the sync file API. Thread-local because the
 * value is set on the scheduler thread immediately before rf_io_read_file_all returns and read by
 * RF immediately after — no other coroutine on that thread runs in between. */
static _Thread_local rf_address g_io_last_len = 0;

rf_address rf_io_get_result_len(void)
{
    return g_io_last_len;
}

static void rf_proc_run_blocking(struct rf_proc_state* st);

/* Do the actual blocking work. Touches only this request. */
static void rf_io_do_work(rf_io_req* req)
{
    if (req->kind == RF_IO_READ_FILE) {
        FILE* f = fopen(req->path, "rb");
        if (f == NULL) { req->result = -1; return; }
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); req->result = -1; return; }
        long sz = ftell(f);
        if (sz < 0) { fclose(f); req->result = -1; return; }
        rewind(f);
        char* buf = (char*)malloc((size_t)sz + 1);
        if (buf == NULL) { fclose(f); req->result = -1; return; }
        size_t n = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        buf[n] = '\0';
        req->data = buf;
        req->result = (int64_t)n;
    }
    else if (req->kind == RF_IO_WRITE_FILE) {
        FILE* f = fopen(req->path, "wb");
        if (f == NULL) { req->result = -1; return; }
        size_t n = (req->data_len > 0)
                       ? fwrite(req->data, 1, (size_t)req->data_len, f)
                       : 0;
        int flush_ok = (fflush(f) == 0);
        fclose(f);
        req->result = (flush_ok && n == (size_t)req->data_len) ? (int64_t)n : -1;
    }
    else {
        rf_proc_run_blocking(req->proc);
    }
}

/* An I/O thread: takes requests in order, runs each, and wakes its coroutine. */
#ifdef _WIN32
static unsigned __stdcall rf_io_thread_main(void* arg)
#else
static void* rf_io_thread_main(void* arg)
#endif
{
    (void)arg;
    for (;;) {
        rf_mutex_lock(&g_io_lock);
        while (g_io_head == NULL) {
            rf_cond_wait_forever(&g_io_work, &g_io_lock);
        }
        rf_io_req* req = g_io_head;
        g_io_head = req->qnext;
        if (g_io_head == NULL) {
            g_io_tail = NULL;
        }
        rf_mutex_unlock(&g_io_lock);

        rf_io_do_work(req);
        /* The wake publishes the request's results: the coroutine reads them once it is resumed. */
        rf_sched_wake(req->sched, req->coro);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* Start the I/O threads on the MAIN thread/stack at process startup (called from rf_runtime_init),
 * so every later coroutine call is pure submit+park. If a thread can't be started, I/O runs inline. */
void rf_io_runtime_init(void)
{
    if (g_io_ok) {
        return;
    }
    rf_mutex_init(&g_io_lock);
    rf_cond_init(&g_io_work);
    int started = 0;
    for (int i = 0; i < RF_IO_THREADS; i++) {
#ifdef _WIN32
        uintptr_t h = _beginthreadex(NULL, 0, rf_io_thread_main, NULL, 0, NULL);
        if (h == 0) { break; }
        CloseHandle((HANDLE)h);
#else
        pthread_t tid;
        if (pthread_create(&tid, NULL, rf_io_thread_main, NULL) != 0) { break; }
        pthread_detach(tid);
#endif
        started++;
    }
    g_io_ok = started > 0;
}

static void rf_io_submit(rf_io_req* req)
{
    rf_mutex_lock(&g_io_lock);
    req->qnext = NULL;
    if (g_io_tail == NULL) {
        g_io_head = req;
    } else {
        g_io_tail->qnext = req;
    }
    g_io_tail = req;
    rf_cond_signal(&g_io_work);
    rf_mutex_unlock(&g_io_lock);
}

/* Run a prepared request to completion: inside a scheduler-driven coroutine, hand it to an I/O thread
 * and park the coroutine (siblings keep running) until the work is done; outside one, run the blocking
 * work inline. Shared by read, write and subprocesses. */
static void rf_io_run(rf_io_req* req)
{
    rf_sched* sched = g_io_ok ? rf_sched_current() : NULL;
    rf_coro* self = g_io_ok ? rf_coro_current() : NULL;

    if (sched != NULL && self != NULL) {
        req->sched = sched;
        req->coro = self;
        /* The I/O thread will wake us cross-thread: arm a cross-waker so the run loop does not read this
         * park as a deadlock, and disarm on resume. */
        rf_sched_arm_cross_waker(sched);
        rf_io_submit(req);
        /* Park EXACTLY ONCE to pair with the exactly-once wake from the I/O thread. The work can complete
         * (and wake us) before we park, in which case rf_sched_wake already recorded the wake and the
         * single rf_sched_park_external switch-out is resumed at once. */
        rf_sched_park_external();
        rf_sched_disarm_cross_waker(sched);
    } else {
        rf_io_do_work(req);
    }
}

/* Allocate a request with a NUL-terminated copy of `path`. Throws (does not return) on OOM. */
static rf_io_req* rf_io_req_new(rf_io_kind kind, const char* path, rf_S32 path_len)
{
    rf_io_req* req = (rf_io_req*)calloc(1, sizeof(rf_io_req));
    if (req == NULL) {
        __rf_throw("OutOfMemoryError", "Failed to allocate async I/O request");
        return NULL; /* unreachable */
    }
    req->kind = kind;
    req->result = -1;

    size_t plen = (path_len < 0) ? strlen(path) : (size_t)path_len;
    req->path = (char*)malloc(plen + 1);
    if (req->path == NULL) {
        free(req);
        __rf_throw("OutOfMemoryError", "Failed to allocate async I/O path");
        return NULL; /* unreachable */
    }
    memcpy(req->path, path, plen);
    req->path[plen] = '\0';
    return req;
}

/* Read an entire file asynchronously. Inside a scheduler-driven coroutine this parks the coroutine
 * (siblings keep running) while an I/O thread does the blocking read; outside one it runs the read
 * inline. Returns a malloc'd, NUL-terminated buffer (caller frees) and sets the result length readable
 * via rf_io_get_result_len; returns NULL on error (length 0). */
char* rf_io_read_file_all(const char* path, rf_S32 path_len)
{
    rf_io_req* req = rf_io_req_new(RF_IO_READ_FILE, path, path_len);
    rf_io_run(req);

    char* data = req->data;
    int64_t result = req->result;
    free(req->path);
    free(req);

    if (result < 0) {
        g_io_last_len = 0;
        return NULL;
    }
    g_io_last_len = (rf_address)result;
    return data;
}

/* Write `data_len` bytes from `data` to `path` (truncating), asynchronously. Inside a scheduler-driven
 * coroutine this parks the coroutine while an I/O thread does the blocking write; outside one it runs
 * inline. The payload is copied into the request so the caller's buffer need not outlive the call.
 * Returns the number of bytes written, or -1 on error. */
int64_t rf_io_write_file_all(const char* path, rf_S32 path_len, const char* data, int64_t data_len)
{
    rf_io_req* req = rf_io_req_new(RF_IO_WRITE_FILE, path, path_len);
    if (data_len < 0) {
        data_len = 0;
    }
    req->data_len = data_len;
    if (data_len > 0) {
        req->data = (char*)malloc((size_t)data_len);
        if (req->data == NULL) {
            free(req->path);
            free(req);
            __rf_throw("OutOfMemoryError", "Failed to allocate async I/O write buffer");
            return -1; /* unreachable */
        }
        memcpy(req->data, data, (size_t)data_len);
    }

    rf_io_run(req);

    int64_t result = req->result;
    free(req->data);
    free(req->path);
    free(req);
    return result;
}

/* ---- Subprocesses: run a command, capture stdout/stderr, get exit code + signal ---------------
 * The child's stdin is the null device, and its stdout and stderr are pipes the parent reads to the
 * end, both at once (so a child filling one pipe while the other is drained can't deadlock), before
 * waiting for it. The command is searched for on PATH as a shell would (execvp / CreateProcessW). */
typedef struct rf_proc_state {
    char** argv;               /* owned, NULL-terminated argv (each entry malloc'd) */
    char* cwd;                 /* owned working directory, or NULL to inherit */
    char** envp;               /* owned, NULL-terminated "KEY=VALUE" env, or NULL to inherit */
    int64_t exit_status;       /* child exit code (meaningful when term_signal == 0) */
    int term_signal;           /* signal that killed the child, or 0 if it exited normally */
    int spawn_err;             /* non-zero if the child could not be started */
    char* out_buf; size_t out_len; size_t out_cap;
    char* err_buf; size_t err_len; size_t err_cap;
} rf_proc_state;

/* Grow-and-append into a malloc'd, NUL-terminated buffer. Best-effort on OOM (drops the chunk). */
static void rf_proc_buf_append(char** buf, size_t* len, size_t* cap, const char* data, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t ncap = (*cap == 0) ? 256 : *cap;
        while (*len + n + 1 > ncap) { ncap *= 2; }
        char* nb = (char*)realloc(*buf, ncap);
        if (nb == NULL) { return; }
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
}

#ifdef _WIN32

/* UTF-8 to a malloc'd UTF-16 string, or NULL. */
static WCHAR* rf_proc_wide(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) { return NULL; }
    WCHAR* w = (WCHAR*)malloc((size_t)n * sizeof(WCHAR));
    if (w == NULL) { return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* Appends one argument to a command line as CommandLineToArgvW reads it back, quoting it only when it
 * needs to be (libuv's quote_cmd_arg): empty, or holding a space, a tab or a quote. `target` has room
 * for 2 * len + 3 characters. Returns the end. */
static WCHAR* rf_proc_quote_arg(const WCHAR* source, WCHAR* target)
{
    size_t len = wcslen(source);
    if (len == 0) {
        *(target++) = L'"';
        *(target++) = L'"';
        return target;
    }
    if (wcspbrk(source, L" \t\"") == NULL) {
        memcpy(target, source, len * sizeof(WCHAR));
        return target + len;
    }
    if (wcspbrk(source, L"\"\\") == NULL) {
        *(target++) = L'"';
        memcpy(target, source, len * sizeof(WCHAR));
        target += len;
        *(target++) = L'"';
        return target;
    }
    /* Backslashes before a quote, or before the closing quote, are doubled; a quote is escaped. Built
     * backwards, then reversed. */
    *(target++) = L'"';
    WCHAR* start = target;
    int quote_hit = 1;
    for (size_t i = len; i > 0; --i) {
        *(target++) = source[i - 1];
        if (quote_hit && source[i - 1] == L'\\') {
            *(target++) = L'\\';
        } else if (source[i - 1] == L'"') {
            quote_hit = 1;
            *(target++) = L'\\';
        } else {
            quote_hit = 0;
        }
    }
    for (WCHAR *a = start, *b = target - 1; a < b; a++, b--) {
        WCHAR t = *a; *a = *b; *b = t;
    }
    *(target++) = L'"';
    return target;
}

/* The whole command line: each argument quoted as needed, separated by spaces. Malloc'd, or NULL. */
static WCHAR* rf_proc_command_line(char** argv)
{
    size_t cap = 1;
    for (int i = 0; argv[i] != NULL; i++) { cap += 2 * strlen(argv[i]) + 4; }
    WCHAR* line = (WCHAR*)malloc(cap * sizeof(WCHAR));
    if (line == NULL) { return NULL; }
    WCHAR* pos = line;
    for (int i = 0; argv[i] != NULL; i++) {
        WCHAR* w = rf_proc_wide(argv[i]);
        if (w == NULL) { free(line); return NULL; }
        if (i > 0) { *(pos++) = L' '; }
        pos = rf_proc_quote_arg(w, pos);
        free(w);
    }
    *pos = L'\0';
    return line;
}

static int rf_proc_env_compare(const void* a, const void* b)
{
    const WCHAR* x = *(const WCHAR* const*)a;
    const WCHAR* y = *(const WCHAR* const*)b;
    return _wcsicmp(x, y);
}

/* A Unicode environment block ("K=V\0...\0\0"), sorted as Windows expects. Malloc'd, or NULL. */
static WCHAR* rf_proc_env_block(char** envp)
{
    int n = 0;
    while (envp[n] != NULL) { n++; }
    WCHAR** wide = (WCHAR**)calloc((size_t)n + 1, sizeof(WCHAR*));
    if (wide == NULL) { return NULL; }
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        wide[i] = rf_proc_wide(envp[i]);
        if (wide[i] == NULL) {
            for (int j = 0; j < i; j++) { free(wide[j]); }
            free(wide);
            return NULL;
        }
        total += wcslen(wide[i]) + 1;
    }
    qsort(wide, (size_t)n, sizeof(WCHAR*), rf_proc_env_compare);
    WCHAR* block = (WCHAR*)malloc(total * sizeof(WCHAR));
    WCHAR* pos = block;
    for (int i = 0; i < n; i++) {
        if (block != NULL) {
            size_t len = wcslen(wide[i]) + 1;
            memcpy(pos, wide[i], len * sizeof(WCHAR));
            pos += len;
        }
        free(wide[i]);
    }
    free(wide);
    if (block != NULL) { *pos = L'\0'; }
    return block;
}

typedef struct rf_proc_reader {
    HANDLE pipe;
    char** buf; size_t* len; size_t* cap;
} rf_proc_reader;

/* Reads a pipe to its end into a buffer. */
static unsigned __stdcall rf_proc_read_all(void* arg)
{
    rf_proc_reader* r = (rf_proc_reader*)arg;
    char chunk[4096];
    DWORD n;
    while (ReadFile(r->pipe, chunk, sizeof chunk, &n, NULL) && n > 0) {
        rf_proc_buf_append(r->buf, r->len, r->cap, chunk, (size_t)n);
    }
    return 0;
}

static void rf_proc_run_blocking(rf_proc_state* st)
{
    st->exit_status = -1;
    st->term_signal = 0;
    WCHAR* line = rf_proc_command_line(st->argv);
    WCHAR* cwd = st->cwd != NULL ? rf_proc_wide(st->cwd) : NULL;
    WCHAR* env = st->envp != NULL ? rf_proc_env_block(st->envp) : NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    int ok = line != NULL && nul != INVALID_HANDLE_VALUE
             && (st->cwd == NULL || cwd != NULL) && (st->envp == NULL || env != NULL)
             && CreatePipe(&out_r, &out_w, &sa, 0) && CreatePipe(&err_r, &err_w, &sa, 0);
    if (ok) {
        /* The parent's ends stay out of the child. */
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    if (ok) {
        STARTUPINFOW si;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = out_w;
        si.hStdError = err_w;
        ok = CreateProcessW(NULL, line, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT, env, cwd, &si, &pi);
    }
    /* The child's ends close here, so the pipes end when the child exits. */
    if (out_w != NULL) { CloseHandle(out_w); }
    if (err_w != NULL) { CloseHandle(err_w); }
    if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); }

    if (ok) {
        rf_proc_reader err_reader = { err_r, &st->err_buf, &st->err_len, &st->err_cap };
        uintptr_t helper = _beginthreadex(NULL, 0, rf_proc_read_all, &err_reader, 0, NULL);
        rf_proc_reader out_reader = { out_r, &st->out_buf, &st->out_len, &st->out_cap };
        rf_proc_read_all(&out_reader);
        if (helper != 0) {
            WaitForSingleObject((HANDLE)helper, INFINITE);
            CloseHandle((HANDLE)helper);
        } else {
            rf_proc_read_all(&err_reader);
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        st->exit_status = (int64_t)code;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        st->spawn_err = 1;
    }
    if (out_r != NULL) { CloseHandle(out_r); }
    if (err_r != NULL) { CloseHandle(err_r); }
    free(line);
    free(cwd);
    free(env);
}

#else /* POSIX */

/* The process environment. A macOS dylib reaches it through _NSGetEnviron (as libuv does). */
#if defined(__APPLE__)
#include <crt_externs.h>
#define RF_ENVIRON (*_NSGetEnviron())
#else
extern char** environ;
#define RF_ENVIRON environ
#endif

/* A pipe with close-on-exec on both ends. */
static int rf_proc_pipe(int fds[2])
{
    if (pipe(fds) != 0) { return -1; }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

static void rf_proc_run_blocking(rf_proc_state* st)
{
    st->exit_status = -1;
    st->term_signal = 0;
    int out_p[2], err_p[2], exec_p[2];
    if (rf_proc_pipe(out_p) != 0) { st->spawn_err = 1; return; }
    if (rf_proc_pipe(err_p) != 0) { close(out_p[0]); close(out_p[1]); st->spawn_err = 1; return; }
    /* The child writes errno here if exec fails; a successful exec closes it (close-on-exec). */
    if (rf_proc_pipe(exec_p) != 0) {
        close(out_p[0]); close(out_p[1]); close(err_p[0]); close(err_p[1]);
        st->spawn_err = 1;
        return;
    }
    int nul = open("/dev/null", O_RDONLY | O_CLOEXEC);

    pid_t pid = fork();
    if (pid == 0) {
        /* The child: only async-signal-safe calls until exec. dup2 clears close-on-exec on the copies. */
        if (nul >= 0) { dup2(nul, 0); }
        dup2(out_p[1], 1);
        dup2(err_p[1], 2);
        if (st->cwd != NULL && chdir(st->cwd) != 0) {
            int e = errno;
            (void)!write(exec_p[1], &e, sizeof e);
            _exit(127);
        }
        if (st->envp != NULL) { RF_ENVIRON = st->envp; }
        execvp(st->argv[0], st->argv);
        int e = errno;
        (void)!write(exec_p[1], &e, sizeof e);
        _exit(127);
    }

    close(out_p[1]);
    close(err_p[1]);
    close(exec_p[1]);
    if (nul >= 0) { close(nul); }
    if (pid < 0) {
        close(out_p[0]); close(err_p[0]); close(exec_p[0]);
        st->spawn_err = 1;
        return;
    }

    int exec_errno = 0;
    ssize_t got;
    do { got = read(exec_p[0], &exec_errno, sizeof exec_errno); } while (got < 0 && errno == EINTR);
    close(exec_p[0]);

    /* Read both pipes to their end, whichever has data. */
    struct pollfd fds[2] = { { out_p[0], POLLIN, 0 }, { err_p[0], POLLIN, 0 } };
    int open_count = 2;
    char chunk[4096];
    while (open_count > 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) { continue; }
            break;
        }
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0 || fds[i].revents == 0) { continue; }
            ssize_t n = read(fds[i].fd, chunk, sizeof chunk);
            if (n > 0) {
                if (i == 0) { rf_proc_buf_append(&st->out_buf, &st->out_len, &st->out_cap, chunk, (size_t)n); }
                else        { rf_proc_buf_append(&st->err_buf, &st->err_len, &st->err_cap, chunk, (size_t)n); }
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                close(fds[i].fd);
                fds[i].fd = -1;
                open_count--;
            }
        }
    }
    for (int i = 0; i < 2; i++) { if (fds[i].fd >= 0) { close(fds[i].fd); } }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (got > 0) {
        st->spawn_err = exec_errno != 0 ? exec_errno : 1;
        return;
    }
    if (WIFEXITED(status)) {
        st->exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        st->exit_status = 0;
        st->term_signal = WTERMSIG(status);
    }
}

#endif

/* Captured subprocess result, handed to RF after rf_proc_run returns. Thread-local for the same
 * reason as g_io_last_len: set on the scheduler thread immediately before rf_proc_run returns and
 * read by RF right after, with no other coroutine running in between. */
static _Thread_local int64_t    g_proc_exit = 0;
static _Thread_local int        g_proc_signal = 0;     /* 0 = exited normally (not signalled) */
static _Thread_local char*      g_proc_out = NULL;
static _Thread_local rf_address g_proc_out_len = 0;
static _Thread_local char*      g_proc_err = NULL;
static _Thread_local rf_address g_proc_err_len = 0;

rf_S32     rf_proc_term_signal(void) { return (rf_S32)g_proc_signal; }
char*      rf_proc_output(void)      { return g_proc_out; }
rf_address rf_proc_output_len(void)  { return g_proc_out_len; }
char*      rf_proc_errors(void)      { return g_proc_err; }
rf_address rf_proc_errors_len(void)  { return g_proc_err_len; }

/* Duplicate `n` bytes of `s` (or strlen(s) if n < 0) into a fresh NUL-terminated string. */
static char* rf_strdupn(const char* s, rf_S32 n)
{
    size_t len = (n < 0) ? strlen(s) : (size_t)n;
    char* d = (char*)malloc(len + 1);
    if (d == NULL) { return NULL; }
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

static rf_proc_state* rf_proc_state_new(void)
{
    rf_proc_state* st = (rf_proc_state*)calloc(1, sizeof(rf_proc_state));
    if (st == NULL) {
        __rf_throw("OutOfMemoryError", "Failed to allocate subprocess state");
        return NULL; /* unreachable */
    }
    st->exit_status = -1;
    st->term_signal = 0;
    return st;
}

static void rf_proc_state_free(rf_proc_state* st)
{
    if (st == NULL) { return; }
    if (st->argv != NULL) {
        for (int i = 0; st->argv[i] != NULL; i++) { free(st->argv[i]); }
        free(st->argv);
    }
    if (st->envp != NULL) {
        for (int i = 0; st->envp[i] != NULL; i++) { free(st->envp[i]); }
        free(st->envp);
    }
    free(st->cwd);
    /* out_buf / err_buf are handed to the RF caller via the thread-locals — NOT freed here. */
    free(st);
}

/* Run a fully-prepared state (argv + optional cwd set), capturing stdout/stderr. Inside a
 * scheduler-driven coroutine the coroutine PARKS while the process runs (siblings progress); on a
 * plain thread it runs on that thread. Publishes the captured streams + status to the thread-locals,
 * frees the state (takes ownership), and returns the child's exit code (-1 if it could not be spawned). */
static int64_t rf_proc_exec(rf_proc_state* st)
{
    rf_io_req* req = (rf_io_req*)calloc(1, sizeof(rf_io_req));
    if (req == NULL) {
        rf_proc_state_free(st);
        __rf_throw("OutOfMemoryError", "Failed to allocate subprocess request");
        return -1; /* unreachable */
    }
    req->kind = RF_IO_RUN_PROCESS;
    req->proc = st;
    rf_io_run(req);

    g_proc_exit    = st->exit_status;
    g_proc_signal  = st->term_signal;
    g_proc_out     = st->out_buf;
    g_proc_out_len = (rf_address)st->out_len;
    g_proc_err     = st->err_buf;
    g_proc_err_len = (rf_address)st->err_len;

    int64_t exit_code = (st->spawn_err != 0) ? -1 : st->exit_status;
    rf_proc_state_free(st);
    free(req);
    return exit_code;
}

/* Shell form: run `command` via the platform shell (sh -c / cmd /c). Convenience for pipes/globs at
 * the cost of shell quoting; prefer the argv builder (rf_proc_begin/...) for untrusted input. */
int64_t rf_proc_run(const char* command, rf_S32 command_len)
{
    rf_proc_state* st = rf_proc_state_new();
    st->argv = (char**)calloc(4, sizeof(char*));
    if (st->argv == NULL) {
        rf_proc_state_free(st);
        __rf_throw("OutOfMemoryError", "Failed to allocate subprocess argv");
        return -1; /* unreachable */
    }
#if defined(_WIN32)
    st->argv[0] = rf_strdupn("cmd.exe", -1); st->argv[1] = rf_strdupn("/c", -1);
#else
    st->argv[0] = rf_strdupn("/bin/sh", -1); st->argv[1] = rf_strdupn("-c", -1);
#endif
    st->argv[2] = rf_strdupn(command, command_len);
    st->argv[3] = NULL;
    return rf_proc_exec(st);
}

/* Argv builder: spawn an executable directly with an explicit argument vector (NO shell — no quoting
 * or injection hazard). rf_proc_begin sets argv[0] = file; add_arg appends; set_cwd is optional;
 * run_built launches and consumes the builder. Mirrors the rf_race_* incremental-FFI pattern. */
typedef struct rf_proc_builder {
    char** argv;
    int argv_count;
    int argv_cap;
    char* cwd;
    char** envv;               /* "KEY=VALUE" override strings (not NULL-terminated; env_count) */
    int env_count;
    int env_cap;
} rf_proc_builder;

/* The parent process environment as a NULL-terminated "KEY=VALUE" array. NOTE (Windows): this is the
 * CRT's ANSI environment — correct for ASCII keys/values (PATH, etc.); non-ASCII values could be
 * mis-encoded since the child's environment is built from UTF-8. Acceptable for the common case. */
#if defined(_WIN32)
extern char** _environ;
  #define RF_PARENT_ENVIRON _environ
#else
  #define RF_PARENT_ENVIRON RF_ENVIRON
#endif

/* Build a NULL-terminated env array = parent environment with `overrides` applied (an override
 * replaces a parent entry with the same KEY, else is appended). The override strings are MOVED into
 * the result; parent entries are copied. Returns NULL if there are no overrides (inherit). */
static char** rf_proc_merge_env(char** overrides, int n)
{
    if (n <= 0) { return NULL; }
    char** parent = RF_PARENT_ENVIRON;
    int pc = 0;
    if (parent != NULL) { while (parent[pc] != NULL) { pc++; } }
    char** env = (char**)malloc((size_t)(pc + n + 1) * sizeof(char*));
    if (env == NULL) { return NULL; }
    int k = 0;
    for (int i = 0; i < pc; i++) {
        const char* peq = strchr(parent[i], '=');
        size_t pkl = peq ? (size_t)(peq - parent[i]) : strlen(parent[i]);
        int overridden = 0;
        for (int j = 0; j < n; j++) {
            const char* oeq = strchr(overrides[j], '=');
            size_t okl = oeq ? (size_t)(oeq - overrides[j]) : strlen(overrides[j]);
            if (okl == pkl && memcmp(overrides[j], parent[i], pkl) == 0) { overridden = 1; break; }
        }
        if (!overridden) { env[k++] = rf_strdupn(parent[i], -1); }
    }
    for (int j = 0; j < n; j++) { env[k++] = overrides[j]; } /* move the override strings in */
    env[k] = NULL;
    return env;
}

rf_proc_builder* rf_proc_begin(const char* file, rf_S32 file_len)
{
    rf_proc_builder* b = (rf_proc_builder*)calloc(1, sizeof(rf_proc_builder));
    if (b == NULL) {
        __rf_throw("OutOfMemoryError", "Failed to allocate subprocess builder");
        return NULL; /* unreachable */
    }
    b->argv_cap = 4;
    b->argv = (char**)calloc((size_t)b->argv_cap, sizeof(char*));
    if (b->argv == NULL) {
        free(b);
        __rf_throw("OutOfMemoryError", "Failed to allocate subprocess argv");
        return NULL; /* unreachable */
    }
    b->argv[0] = rf_strdupn(file, file_len); /* argv[0] = the executable */
    b->argv_count = 1;
    return b;
}

void rf_proc_add_arg(rf_proc_builder* b, const char* arg, rf_S32 arg_len)
{
    if (b == NULL) { return; }
    if (b->argv_count + 2 > b->argv_cap) { /* +1 for the new arg, +1 for the trailing NULL */
        int ncap = b->argv_cap * 2;
        char** na = (char**)realloc(b->argv, (size_t)ncap * sizeof(char*));
        if (na == NULL) { return; }
        b->argv = na;
        b->argv_cap = ncap;
    }
    b->argv[b->argv_count] = rf_strdupn(arg, arg_len);
    b->argv_count++;
    b->argv[b->argv_count] = NULL;
}

void rf_proc_set_cwd(rf_proc_builder* b, const char* cwd, rf_S32 cwd_len)
{
    if (b == NULL) { return; }
    free(b->cwd);
    b->cwd = NULL;
    size_t len = (cwd_len < 0) ? strlen(cwd) : (size_t)cwd_len;
    if (len > 0) { b->cwd = rf_strdupn(cwd, (rf_S32)len); } /* empty = inherit (leave NULL) */
}

/* Add an environment override "KEY=VALUE". These are MERGED into (not replacing) the parent
 * environment at launch — so setting one var keeps PATH and the rest intact. */
void rf_proc_add_env(rf_proc_builder* b, const char* key, rf_S32 key_len, const char* val, rf_S32 val_len)
{
    if (b == NULL) { return; }
    if (b->env_count + 1 > b->env_cap) {
        int ncap = (b->env_cap == 0) ? 4 : b->env_cap * 2;
        char** ne = (char**)realloc(b->envv, (size_t)ncap * sizeof(char*));
        if (ne == NULL) { return; }
        b->envv = ne;
        b->env_cap = ncap;
    }
    size_t kl = (key_len < 0) ? strlen(key) : (size_t)key_len;
    size_t vl = (val_len < 0) ? strlen(val) : (size_t)val_len;
    char* entry = (char*)malloc(kl + 1 + vl + 1);
    if (entry == NULL) { return; }
    memcpy(entry, key, kl);
    entry[kl] = '=';
    memcpy(entry + kl + 1, val, vl);
    entry[kl + 1 + vl] = '\0';
    b->envv[b->env_count] = entry;
    b->env_count++;
}

int64_t rf_proc_run_built(rf_proc_builder* b)
{
    if (b == NULL) { return -1; }
    rf_proc_state* st = rf_proc_state_new();
    st->argv = b->argv; /* transfer ownership of the array + strings */
    st->cwd = b->cwd;
    st->envp = rf_proc_merge_env(b->envv, b->env_count); /* moves override strings into st->envp */
    free(b->envv); /* the override strings were moved into st->envp; free only the array */
    free(b);
    return rf_proc_exec(st);
}
