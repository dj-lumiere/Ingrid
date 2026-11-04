#!/bin/bash
set -e

echo "Building Revare Native Libraries..."

mkdir -p build
cd build

# One build in this tree at a time: a builder building a program in a checkout (NativeToolchain) runs cmake here
# too and holds the same lock, so two test suites side by side don't configure it under each other.
if command -v flock >/dev/null 2>&1; then
    exec 9>".build.lock"
    flock 9
fi

echo "Configuring with CMake..."
# LAPILA_DLL (set by the Revare build) is the Lapila builder that compiles ../runtime-lapila; without it CMake
# looks for the checkout's own Lapila build.
LAPILA_ARGS=()
if [[ -n "${LAPILA_DLL:-}" ]]; then
    LAPILA_ARGS=(-DLAPILA_DLL="$LAPILA_DLL")
fi
cmake .. -DCMAKE_BUILD_TYPE=Release "${LAPILA_ARGS[@]}"

echo "Building libraries..."
cmake --build . --config Release

echo "Copying libraries to project directories..."
mkdir -p ../../bin/Debug/net10.0
mkdir -p ../../bin/Release/net10.0

# Copy shared libraries based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    cp lib/*.dylib ../../bin/Debug/net10.0/ 2>/dev/null || true
    cp lib/*.dylib ../../bin/Release/net10.0/ 2>/dev/null || true
else
    # Linux
    cp lib/*.so ../../bin/Debug/net10.0/ 2>/dev/null || true
    cp lib/*.so ../../bin/Release/net10.0/ 2>/dev/null || true
fi

echo "Native libraries built successfully!"
