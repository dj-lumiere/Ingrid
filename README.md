<p align="center">
  <img src="branding/ingrid.svg" alt="Ingrid logo" width="112">
</p>

<h1 align="center">Ingrid</h1>

<p align="center"><strong>The foundation library for Revare and Mielia.</strong></p>

[Revare](https://github.com/dj-lumiere/Revare) and [Mielia](https://github.com/dj-lumiere/Mielia) share
one standard library underneath their own, and Ingrid is that layer. It is written in
[Lapila](https://github.com/dj-lumiere/Lapila), with no C of its own: the C library and the operating system are
called as external libraries. It has two parts.

**The runtime library** (`runtime-lapila/`, built into `revare_runtime`), for what the languages can't reach
on their own:

- crash output and stack traces
- console and file I/O, and blocking I/O on the runtime's own threads
- coroutines, the scheduler, OS threads, channels, and monitors
- time, random numbers, and process signals

**Code linked into every program** (`lapila/`): heap allocation, word division for the wide integers, the
Roamed entity runtime and its cycle collector, the crash handler, SipHash, the xoshiro256** generator with its
splitmix64 seeding, JSON string escaping, the text pattern engine (Lapila's `Text/Patterns`, which compiles a
pattern and searches text in time that grows with the text's length), the library functions of the binary floats B16, B32, B64, and B128 (Lapila's correctly rounded routines), the B128
arithmetic and the text of the binary floats and decimals (writing and reading), the math of the complex
numbers, quaternions, and the 2x2, 3x3, and 4x4 matrices (their inverses and A x = b by Gaussian elimination with partial pivoting), and the decimal floats D32, D64, and D128 (`lapila/decimal/`: their
arithmetic, rounding, conversions, DPD encoding, and correctly rounded transcendentals, one engine for the three
widths), and the arbitrary-precision Int (`lapila/integer/`, with the scratch arena the engine takes its
temporaries from in `lapila/integer/scratch.lpa`, and its exports in `lapila/integer_exports/`). More
math, hashing, cryptography, and algorithms move here next, written once so that both languages build on one
implementation.

It is written in Lapila here, and shipped as source with the rest.

The standard library owns the language-facing API; Ingrid supplies what sits under it.

## Layout

```
runtime-lapila/  # The runtime library (module Ingrid::Runtime)
lapila/          # Linked into every Revare and Mielia module
├── decimal/      # The decimal floats D32, D64, and D128
└── integer/      # The arbitrary-precision Int: limb layer, NTT multiply, division, square root, text
tests/            # Golden tests of lapila/, run by the Lapila builder
native/
├── CMakeLists.txt
├── build.sh      # Build on Linux and macOS
└── build.bat     # Build on Windows
```

## Building

Ingrid builds with CMake 3.20+ and Clang (plus Ninja on Windows), and the Lapila builder. It needs no
third-party library. With [Lapila](https://github.com/dj-lumiere/Lapila) checked out next to Ingrid and built
(`dotnet build`), CMake finds the builder on its own; otherwise set `LAPILA_DLL` to `lapila.dll`:

```bash
cd native && ./build.sh        # Windows: build.bat
```

You rarely need to do this by hand: building [Anvila](https://github.com/dj-lumiere/Anvila) (or either
language project) builds Ingrid first, and `StageIngridRuntime.targets` copies the built libraries
next to each executable. The full workspace setup is in the
[Revare README](https://github.com/dj-lumiere/Revare#from-source).

## Tests

`tests/` holds golden tests of the code in `lapila/`, run by the Lapila builder's test runner: SipHash-2-4 against
the reference test vectors, splitmix64, xoshiro256** and the JSON escaping against Revare's own streams and
text, the binary-float exports against correctly rounded results from mpmath, the float and decimal text against
what Revare's own text routines printed,
the complex numbers, quaternions, and matrices against Revare's bodies
before they moved here (and the crashes their checked arithmetic reports), the fast paths of C128's products and
quotients against the exact paths they fall back to, the matrix inverses and solutions of
A x = b against a Python model of the same elimination in B32, the D32, D64, and D128 arithmetic
against Revare's engines and the other decimal operations against exact references, rounding once at D32 and
D64 against rounding through D128 (why the narrow formats don't borrow D128's results), the decimal
transcendentals against mpmath, Int through its C calls (and the scratch and result-buffer retries)
against what Revare's engines computed, the word division on its edge cases, the text patterns through their C calls (compiling a pattern, searching, the
capture slots and the messages of patterns that don't compile), the Roamed counts and lock, the cycle
collector, the heap calls, and the crash report, and from
`runtime-lapila/` the SignalCaster (`monitor.lpa`), the channels' last-handle drop, the per-thread file results, the
coroutine cancellation stack, the JIT's emulated thread-locals, and the clock and random word. Each test is a directory with a `config.toml` that lists the
files it builds with (`sources`), and `<name>.expected` (standard output) and `<name>.exit` (exit status) next to it.
The routines a test reaches in the Revare runtime library (`rf_crash_exit`, the trace, the current task, the
scheduler's park and wake, ...) are defined in the test itself, so a test needs nothing but the builder. With Lapila
checked out next to Ingrid and built (`dotnet build`), from Ingrid's directory:

```bash
dotnet ../Lapila/bin/Debug/net10.0/lapila.dll test tests
dotnet ../Lapila/bin/Debug/net10.0/lapila.dll check runtime-lapila/*.lpa   # also lapila/*.lpa lapila/decimal/*.lpa lapila/integer/*.lpa lapila/integer_exports/*.lpa, --target <triple>
dotnet ../Lapila/bin/Debug/net10.0/lapila.dll fmt --check runtime-lapila lapila tests
dotnet ../Lapila/bin/Debug/net10.0/lapila.dll lint runtime-lapila lapila tests
```

CI runs these on Linux, Windows, and macOS, beside the workspace build that tests Ingrid through Revare and
Mielia.

## License

MIT; see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
