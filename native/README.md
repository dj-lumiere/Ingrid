# Ingrid's Native Build

This directory builds the runtime library, `razorforge_runtime` (`.dll` / `.so` / `.dylib`), that every RazorForge
and Suflae program links against. The library has no C source: it is Ingrid's Tessera code in
[`../runtime-tessera`](../runtime-tessera), compiled by the Tessera builder to LLVM IR, and by clang to the
library's one object. The C library and the operating system are called as external libraries
(`#external("c")`), and nothing else is linked.

The rule: no C of our own. C comes back only where a guarantee forces it (constant-time cryptography in an
audited library, say), never for convenience.

## Two Parts of Ingrid

- [`../runtime-tessera`](../runtime-tessera) (module `Ingrid::Runtime`): the runtime library. Code with process-wide
  state, or that the in-process JIT resolves by name, lives here.
- [`../tessera`](../tessera): Tessera code llvm-linked into every RazorForge and Suflae module, ahead of time and
  in the JIT, so the optimizer sees through it (the allocators to malloc / calloc / free, the word division to
  `divq`).

## Layout

```text
native/
├── CMakeLists.txt  // tessera build → .ll → clang → the library's object → razorforge_runtime
├── build.bat
└── build.sh
../runtime-tessera/
├── sync.tess  // what the rest shares: C calls, a condition variable, the clock, the panic handler
├── trace.tess  // the crash: __rf_throw, stack traces, fault handlers, the standard streams
├── console.tess  // show / alert / ask, crash output and exit, runtime init, C-string helpers
├── files.tess  // files by handle, memory-mapped views, the filesystem (shared part)
├── files_windows.tess  // files.tess on Windows (Win32)
├── files_posix.tess  // files.tess on Linux and macOS
├── numeric.tess  // libm's exact float operations, a random word, the wall clock, FFI test helpers
├── signals.tess  // when_interrupted / when_terminated
├── builtins.tess  // what the JIT would otherwise get from compiler-rt: 128-bit division, emulated TLS
├── coro.tess  // a coroutine: create / resume / yield / abandon, the cancellation shadow stack
├── sched.tess  // the scheduler: the worker pool, deques and stealing, timers, park/wake
├── race.tess  // race!
├── task.tess  // result tasks and threaded tasks
├── channel.tess  // channels
├── monitor.tess  // SignalCaster
├── io.tess  // whole-file reads and writes and subprocesses on the I/O threads
└── context.tess  // the older context entry points, and waitfor on a thread
../tessera/
├── memory.tess  // rf_allocate_dynamic and the rest of the heap calls, scratch regions
├── divide.tess  // word division for the wide integers
├── random.tess  // splitmix64
├── roam.tess  // the Roamed entity runtime: counts, promotion, the task-keyed lock
├── cycle.tess  // the cycle collector for Roamed entities
├── deadlock.tess  // the opt-in deadlock detector for the Roamed lock
└── panic.tess  // the panic handler of every program
```

Platform differences are per-routine `#target(os: ...)` declarations, not separate builds: one source set type-checks
for every target (`tessera check --target <triple> ../runtime-tessera/*.tess`).

## What It Links

- Windows: `msvcrt` (the UCRT and vcruntime, and the DLL's entry point: with no C source, no object asks for them),
  `legacy_stdio_definitions` (`fprintf`, which the UCRT headers inline), `dbghelp` (stack traces),
  `synchronization` (`WaitOnAddress`), `bcrypt` (random words).
- Linux: `m`, `pthread`.
- macOS: libSystem.

No third-party library. `libco` and `libuv` were replaced by Ingrid's own coroutines and I/O threads, the same on
every operating system.

## Building

It needs CMake 3.20+, clang (plus Ninja on Windows), and the Tessera builder: with Tessera checked out next to Ingrid
and built, CMake finds it; otherwise pass `-DTESSERA_DLL=<path to tessera.dll>`.

```bash
./native/build.sh      # Windows: native\build.bat
```

Building Anvila (or either language) builds this first, and `StageIngridRuntime.targets` copies the library next to
each executable.

## Rules

1. The standard library owns the language-facing API, the builder owns lowering, and Ingrid owns platform work and
   runtime state, behind a narrow `rf_*` boundary.
2. Don't expose an external library's API to the builder or the standard library: wrap it in an `rf_*` routine.
3. An exported name is a contract with the builder (`RuntimeContract.cs`) or the standard library's declarations;
   renaming it means editing both sides.
