#!/bin/bash
set -e

echo "Building RazorForge Native Libraries..."

mkdir -p build
cd build

echo "Configuring with CMake..."
# TESSERA_DLL (set by the RazorForge build) is the Tessera builder that compiles ../runtime-tessera; without it CMake
# looks for the checkout's own Tessera build.
TESSERA_ARGS=()
if [[ -n "${TESSERA_DLL:-}" ]]; then
    TESSERA_ARGS=(-DTESSERA_DLL="$TESSERA_DLL")
fi
cmake .. -DCMAKE_BUILD_TYPE=Release "${TESSERA_ARGS[@]}"

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
