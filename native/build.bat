@echo off
setlocal

echo Building Revare Native Libraries...

if not exist build mkdir build
cd build

echo Configuring with CMake...

REM Prefer the clang on PATH (CI installs LLVM 20 there; matches the toolchain
REM the compiler itself shells out to). Fall back to the default LLVM install
REM location for dev machines whose PATH lacks clang.
set "RA_CLANG=clang"
set "RA_CLANGXX=clang++"
where clang >nul 2>nul
if %ERRORLEVEL% neq 0 (
    set "RA_CLANG=C:/Program Files/LLVM/bin/clang.exe"
    set "RA_CLANGXX=C:/Program Files/LLVM/bin/clang++.exe"
)

REM LAPILA_DLL (set by the Revare build) is the Lapila builder that compiles ..\runtime-lapila; without it
REM CMake looks for the checkout's own Lapila build.
set "RA_LAPILA="
if not "%LAPILA_DLL%"=="" set "RA_LAPILA=-DLAPILA_DLL=%LAPILA_DLL%"

cmake .. -G "Ninja" -DCMAKE_C_COMPILER="%RA_CLANG%" -DCMAKE_CXX_COMPILER="%RA_CLANGXX%" -DCMAKE_BUILD_TYPE=Release %RA_LAPILA%

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo Building libraries...
cmake --build . --config Release

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Copying libraries to project directories...
if not exist ..\..\bin\Debug\net10.0 mkdir ..\..\bin\Debug\net10.0
if not exist ..\..\bin\Release\net10.0 mkdir ..\..\bin\Release\net10.0

REM With Ninja generator, output is directly in lib\ and bin\ (no Release subfolder)
copy lib\*.dll ..\..\bin\Debug\net10.0\ 2>nul
copy lib\*.dll ..\..\bin\Release\net10.0\ 2>nul
copy bin\*.dll ..\..\bin\Debug\net10.0\ 2>nul
copy bin\*.dll ..\..\bin\Release\net10.0\ 2>nul

REM Also try Release subfolder in case using Visual Studio generator
copy lib\Release\*.dll ..\..\bin\Debug\net10.0\ 2>nul
copy lib\Release\*.dll ..\..\bin\Release\net10.0\ 2>nul
copy bin\Release\*.dll ..\..\bin\Debug\net10.0\ 2>nul
copy bin\Release\*.dll ..\..\bin\Release\net10.0\ 2>nul

echo Native libraries built successfully!