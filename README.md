<p align="center">
  <img src="branding/ingrid.svg" alt="Ingrid logo" width="112">
</p>

<h1 align="center">Ingrid</h1>

<p align="center"><strong>The foundation library for RazorForge and Suflae.</strong></p>

[RazorForge](https://github.com/dj-lumiere/RazorForge) and [Suflae](https://github.com/dj-lumiere/Suflae) share
one standard library underneath their own, and Ingrid is that layer. It is written in
[Tessera](https://github.com/dj-lumiere/Tessera), with no C of its own: the C library and the operating system are
called as external libraries. It has two parts.

**The runtime library** (`runtime-tessera/`, built into `razorforge_runtime`), for what the languages can't reach
on their own:

- crash output and stack traces
- console and file I/O, and blocking I/O on the runtime's own threads
- coroutines, the scheduler, OS threads, channels, and monitors
- time, random numbers, and process signals

**Code linked into every program** (`tessera/`): heap allocation, word division for the wide integers, the
Roamed entity runtime and its cycle collector, and the crash handler. Math, hashing, cryptography, and algorithms
move here next, written once so that both languages build on one implementation.

The standard library owns the language-facing API; Ingrid supplies what sits under it.

## Layout

```
runtime-tessera/  # The runtime library (module Ingrid::Runtime)
tessera/          # Linked into every RazorForge and Suflae module
tests/            # Golden tests of tessera/, run by the Tessera builder
native/
├── CMakeLists.txt
├── build.sh      # Build on Linux and macOS
└── build.bat     # Build on Windows
```

## Building

Ingrid builds with CMake 3.20+ and Clang (plus Ninja on Windows), and the Tessera builder. It needs no
third-party library. With [Tessera](https://github.com/dj-lumiere/Tessera) checked out next to Ingrid and built
(`dotnet build`), CMake finds the builder on its own; otherwise set `TESSERA_DLL` to `tessera.dll`:

```bash
cd native && ./build.sh        # Windows: build.bat
```

You rarely need to do this by hand: building [Anvila](https://github.com/dj-lumiere/Anvila) (or either
language project) builds Ingrid first, and `StageIngridRuntime.targets` copies the built libraries
next to each executable. The full workspace setup is in the
[RazorForge README](https://github.com/dj-lumiere/RazorForge#from-source).

## Tests

`tests/` holds golden tests of the code in `tessera/`, run by the Tessera builder's test runner: SipHash-2-4 against
the reference test vectors, splitmix64, the word division on its edge cases, the Roamed counts and lock, the cycle
collector, the deadlock detector, the heap calls, and the crash report. Each test is a directory with a `config.toml`
that lists the `tessera/` files it builds with (`sources`), and `<name>.expected` (standard output) and `<name>.exit`
(exit status) next to it. The routines a test reaches in the RazorForge runtime library (`rf_crash_exit`, the
trace, the current task, ...) are defined in the test itself, so a test needs nothing but the builder. With Tessera
checked out next to Ingrid and built (`dotnet build`), from Ingrid's directory:

```bash
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll test tests
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll check runtime-tessera/*.tess   # also tessera/*.tess, --target <triple>
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll fmt --check runtime-tessera tessera tests
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll lint runtime-tessera tessera tests
```

CI runs these on Linux, Windows, and macOS, beside the workspace build that tests Ingrid through RazorForge and
Suflae.

## License

MIT; see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
