cmake_minimum_required(VERSION 3.20)
project(RazorForgeNative C)

# ============================================================================
# The runtime library (razorforge_runtime: .dll / .so / .dylib) is Ingrid's Tessera code in
# ../runtime-tessera, and no C: the Tessera builder compiles it to LLVM IR, and clang compiles that into
# the library's one object. The C library and the operating system are called as external libraries.
# Third-party libraries: none.
# ============================================================================

# Export every symbol on Windows (the import library RazorForge programs link against, and the export
# table the in-process JIT resolves against).
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# The Tessera builder: TESSERA_DLL, which the RazorForge build passes in, or a checkout's own Tessera build.
if(NOT TESSERA_DLL)
    foreach(config Debug Release)
        set(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../../Tessera/bin/${config}/net10.0/tessera.dll")
        if(EXISTS "${candidate}")
            set(TESSERA_DLL "${candidate}")
        endif()
    endforeach()
endif()
if(NOT TESSERA_DLL OR NOT EXISTS "${TESSERA_DLL}")
    message(FATAL_ERROR "The Tessera builder is needed for ../runtime-tessera: build ../../Tessera "
                        "(dotnet build), or pass -DTESSERA_DLL=<path to tessera.dll>.")
endif()
message(STATUS "Tessera builder: ${TESSERA_DLL}")

file(GLOB INGRID_RUNTIME_TESSERA CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../runtime-tessera/*.tess")
file(GLOB_RECURSE TESSERA_STDLIB CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../Tessera/Standard/*.tess")
set(INGRID_RUNTIME_LL "${CMAKE_BINARY_DIR}/ingrid_runtime_tessera.ll")
set(INGRID_RUNTIME_OBJ "${CMAKE_BINARY_DIR}/ingrid_runtime_tessera${CMAKE_C_OUTPUT_EXTENSION}")
# The standard library's exports come along: the F16 and BF16 conversions LLVM calls on Windows, which the
# JIT resolves from this library. Its default panic handler gives way to the runtime's own.
add_custom_command(
    OUTPUT "${INGRID_RUNTIME_LL}"
    COMMAND dotnet "${TESSERA_DLL}" build --emit-llvm --mode release
            ${INGRID_RUNTIME_TESSERA} -o "${INGRID_RUNTIME_LL}"
    DEPENDS ${INGRID_RUNTIME_TESSERA} ${TESSERA_STDLIB} "${TESSERA_DLL}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    COMMENT "Compiling Ingrid's runtime Tessera code"
    VERBATIM)
if(WIN32)
    set(INGRID_RUNTIME_PIC "")
else()
    set(INGRID_RUNTIME_PIC "-fPIC")
endif()
# Only clang reads LLVM IR: GCC takes the .ll for a linker input and makes no object. The C compiler is used when it
# is clang, else the clang on PATH.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(INGRID_CLANG "${CMAKE_C_COMPILER}")
else()
    find_program(INGRID_CLANG clang)
    if(NOT INGRID_CLANG)
        message(FATAL_ERROR "Compiling Ingrid's runtime needs clang (it compiles LLVM IR), and the C compiler is "
                            "${CMAKE_C_COMPILER_ID}: put clang on PATH, or configure with CC=clang.")
    endif()
endif()
add_custom_command(
    OUTPUT "${INGRID_RUNTIME_OBJ}"
    COMMAND "${INGRID_CLANG}" -c -O2 ${INGRID_RUNTIME_PIC} -Wno-override-module "${INGRID_RUNTIME_LL}"
            -o "${INGRID_RUNTIME_OBJ}"
    DEPENDS "${INGRID_RUNTIME_LL}"
    COMMENT "Compiling ingrid_runtime_tessera.ll"
    VERBATIM)
set_source_files_properties("${INGRID_RUNTIME_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_library(razorforge_runtime SHARED "${INGRID_RUNTIME_OBJ}")
set_target_properties(razorforge_runtime PROPERTIES LINKER_LANGUAGE C)

# The libraries the runtime calls into, beyond the C library.
if(WIN32)
    # The C runtime (msvcrt brings the UCRT and vcruntime, and the DLL's entry point): with no C source in the
    # library, no object asks for it. dbghelp: stack traces; synchronization: WaitOnAddress; bcrypt: random words.
    target_link_libraries(razorforge_runtime msvcrt dbghelp synchronization bcrypt)
elseif(APPLE)
    # libSystem has everything
else()
    target_link_libraries(razorforge_runtime m pthread)
endif()

set_target_properties(razorforge_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

install(TARGETS razorforge_runtime
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

message(STATUS "")
message(STATUS "RazorForge native runtime: Tessera (../runtime-tessera), no C")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}, LLVM IR compiled by ${INGRID_CLANG}")
message(STATUS "")
