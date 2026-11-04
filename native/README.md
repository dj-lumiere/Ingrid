# Ingrid's Native Build

This directory builds the runtime library, `revare_runtime` (`.dll` / `.so` / `.dylib`), that every Revare
and Mielia program links against. The library has no C source: it is Ingrid's Lapila code in
[`../runtime-lapila`](../runtime-lapila), compiled by the Lapila builder to LLVM IR, and by clang to the
library's one object. The C library and the operating system are called as external libraries
(`#external("c")`), and nothing else is linked.

The rule: no C of our own. C comes back only where a guarantee forces it (constant-time cryptography in an
audited library, say), never for convenience.

## Two Parts of Ingrid

- [`../runtime-lapila`](../runtime-lapila) (module `Ingrid::Runtime`): the runtime library. Code with process-wide
  state, or that the in-process JIT resolves by name, lives here.
- [`../lapila`](../lapila): Lapila code llvm-linked into every Revare and Mielia module, ahead of time and
  in the JIT, so the optimizer sees through it (the allocators to malloc / calloc / free, the word division to
  `divq`).

## Layout

```text
native/
├── CMakeLists.txt  // lapila build → .ll → clang → the library's object → revare_runtime
├── build.bat
└── build.sh
../runtime-lapila/
├── sync.li  // what the rest shares: C calls, a condition variable, the clock, the crash handler
├── trace.li  // the crash: __rf_throw, stack traces, fault handlers, the standard streams
├── console.li  // show / alert / ask, crash output and exit, runtime init, C-string helpers
├── files.li  // the rf_* files and filesystem calls over Lapila's Standard::Os: handles, mappings, listings
├── numeric.li  // libm's exact float operations, a random word, the wall clock, FFI test helpers
├── signals.li  // CpuEventManager: when_interrupted / when_terminated
├── builtins.li  // what the JIT would otherwise get from compiler-rt: emulated TLS
├── coro.li  // a coroutine: create / resume / yield / abandon, the cancellation shadow stack
├── sched.li  // the scheduler: the worker pool, deques and stealing, timers, park/wake
├── race.li  // race!
├── task.li  // result tasks and threaded tasks
├── channel.li  // channels
├── monitor.li  // SignalCaster
├── io.li  // whole-file reads and writes and subprocesses on the I/O threads
└── context.li  // the older context entry points, and waitfor on a thread
../lapila/
├── memory.li  // rf_allocate_dynamic and the rest of the heap calls, scratch regions
├── divide.li  // word division for the wide integers
├── random.li  // splitmix64
├── hash.li  // SipHash-2-4, behind every hash(k0, k1) in the standard libraries
├── roam.li  // the Roamed entity runtime: counts, promotion, the task-keyed lock
├── cycle.li  // the cycle collector for Roamed entities
├── deadlock.li  // the opt-in deadlock detector for the Roamed lock
└── crash.li  // the crash handler of every program
```

Platform differences are per-routine `#target(os: ...)` declarations, not separate builds: one source set type-checks
for every target (`lapila check --target <triple> ../runtime-lapila/*.li`).

## What It Links

- Windows: `msvcrt` (the UCRT and vcruntime, and the DLL's entry point: with no C source, no object asks for them),
  `dbghelp` (stack traces),
  `synchronization` (`WaitOnAddress`), `bcrypt` (random words).
- Linux: `m`, `pthread`.
- macOS: libSystem.

No third-party library. `libco` and `libuv` were replaced by Ingrid's own coroutines and I/O threads, the same on
every operating system.

## Building

It needs CMake 3.20+, clang (plus Ninja on Windows), and the Lapila builder: with Lapila checked out next to Ingrid
and built, CMake finds it; otherwise pass `-DLAPILA_DLL=<path to lapila.dll>`.

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
