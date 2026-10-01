<p align="center">
  <img src="branding/ingrid.svg" alt="Ingrid logo" width="112">
</p>

<h1 align="center">Ingrid</h1>

<p align="center"><strong>The foundation library for RazorForge and Suflae.</strong></p>

[RazorForge](https://github.com/dj-lumiere/RazorForge) and [Suflae](https://github.com/dj-lumiere/Suflae) share
one standard library underneath their own, and Ingrid is that layer. It has two parts.

**C APIs**, most of them thin layers over the operating system, for what the languages can't reach on their own:

- memory allocation, crash output, and stack traces
- console, file, and async I/O
- coroutines, OS threads, channels, locks, and waiting
- time, random numbers, and process signals
- numeric support that plain LLVM intrinsics do not cover

**Libraries written in [Tessera](https://github.com/dj-lumiere/Tessera)**: math, hashing, cryptography, and
algorithms, written once so that both languages build on one implementation instead of each carrying its own.
They come next; the C APIs are what every program links against today.

The standard library owns the language-facing API; Ingrid supplies what sits under it.

## Layout

```
native/
├── runtime/   # Runtime sources (.c)
├── include/   # Public headers
├── cmake/     # Build configuration
├── tests/     # Native tests
├── build.sh   # Build on Linux and macOS
└── build.bat  # Build on Windows
```

## Building

Ingrid builds with CMake 3.20+ and Clang (plus Ninja on Windows), against [libuv](https://github.com/libuv/libuv)
and [libco](https://github.com/higan-emu/libco), which are not vendored:

```bash
git clone --depth 1 https://github.com/libuv/libuv.git native/libuv
git clone --depth 1 https://github.com/higan-emu/libco.git native/libco
cd native && ./build.sh        # Windows: build.bat
```

You rarely need to do this by hand: building [Anvila](https://github.com/dj-lumiere/Anvila) (or either
language project) builds Ingrid first, and `StageIngridRuntime.targets` copies the built libraries
next to each executable. The full workspace setup is in the
[RazorForge README](https://github.com/dj-lumiere/RazorForge#from-source).

## License

MIT; see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
