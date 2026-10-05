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
Roamed entity runtime and its cycle collector, the crash handler, SipHash, the xoshiro256** generator with its
splitmix64 seeding, JSON string escaping, the library functions of the binary floats B16, B32, B64, and B128 (Tessera's correctly rounded routines), the math of the complex
numbers, quaternions, and 4x4 matrices, and the decimal floats D32, D64, and D128 (`tessera/decimal/`: their
arithmetic, rounding, conversions, DPD encoding, and correctly rounded transcendentals, one engine for the three
widths), and the arbitrary-precision Integer (`tessera/generated/numerics.tess`, with the scratch arena
the engine takes its temporaries from in `tessera/scratch.tess`). More
math, hashing, cryptography, and algorithms move here next, written once so that both languages build on one
implementation.

It is written in Tessera here, except the big numeric engine whose source stays RazorForge (the arbitrary-precision
Integer), which is written into `tessera/generated/` by RazorForge's Tessera backend: `RazorForge export-ingrid`
builds each export file of RazorForge's `IngridExport/` and writes its `ingrid_*` routines, with everything they
reach, as one Tessera file, committed and shipped like the rest.

The standard library owns the language-facing API; Ingrid supplies what sits under it.

## Layout

```
runtime-tessera/  # The runtime library (module Ingrid::Runtime)
tessera/          # Linked into every RazorForge and Suflae module
├── decimal/      # The decimal floats D32, D64, and D128
└── generated/    # Written by `RazorForge export-ingrid`: Integer (numerics.tess)
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
the reference test vectors, splitmix64, xoshiro256** and the JSON escaping against RazorForge's own streams and
text, the binary-float exports against correctly rounded results from mpmath,
the complex numbers, quaternions, and matrices against RazorForge's bodies
before they moved here (and the crashes their checked arithmetic reports), the D32, D64, and D128 arithmetic
against RazorForge's engines and the other decimal operations against exact references, rounding once at D32 and
D64 against rounding through D128 (why the narrow formats don't borrow D128's results), the decimal
transcendentals against mpmath, Integer through its C calls (and the scratch and result-buffer retries)
against what RazorForge's engines computed, the word division on its edge cases, the Roamed counts and lock, the cycle
collector, the deadlock detector, the heap calls, and the crash report, and from
`runtime-tessera/` the SignalCaster (`monitor.tess`), the channels' last-handle drop, the per-thread file results, the
coroutine cancellation stack, the JIT's emulated thread-locals, and the clock and random word. Each test is a directory with a `config.toml` that lists the
files it builds with (`sources`), and `<name>.expected` (standard output) and `<name>.exit` (exit status) next to it.
The routines a test reaches in the RazorForge runtime library (`rf_crash_exit`, the trace, the current task, the
scheduler's park and wake, ...) are defined in the test itself, so a test needs nothing but the builder. With Tessera
checked out next to Ingrid and built (`dotnet build`), from Ingrid's directory:

```bash
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll test tests
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll check runtime-tessera/*.tess   # also tessera/*.tess tessera/decimal/*.tess tessera/generated/*.tess, --target <triple>
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll fmt --check runtime-tessera tessera tests
dotnet ../Tessera/bin/Debug/net10.0/tessera.dll lint runtime-tessera tessera tests
```

CI runs these on Linux, Windows, and macOS, beside the workspace build that tests Ingrid through RazorForge and
Suflae.

## License

MIT; see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
