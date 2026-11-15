# Copies the MinGW DLLs an .exe needs (libstdc++, libcurl and everything
# they pull in) next to it. Run after the build with:
#   cmake -DEXE=<the .exe> -DMINGW_BIN=<the compiler's bin> -DOBJDUMP=<objdump> -P copy_mingw_dlls.cmake
#
# Windows looks for a DLL next to the .exe before it looks on PATH, so this
# stops it picking up another program's older copy instead, e.g. QEMU's
# libstdc++-6.dll, which lacks symbols newer GCCs use and makes the app fail
# with "The procedure entry point ... could not be located".

get_filename_component(EXE_DIR "${EXE}" DIRECTORY)

# Match the exclusions below against paths with forward slashes
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    # Searched before PATH, so the compiler's own DLLs win
    DIRECTORIES "${EXE_DIR}" "${MINGW_BIN}"
    # Windows' own, which it always loads from System32
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ss]ystem32[/\\\\]" "[/\\\\][Ww]indows[/\\\\]"
    RESOLVED_DEPENDENCIES_VAR dlls
    UNRESOLVED_DEPENDENCIES_VAR missing
)

foreach(dll IN LISTS dlls)
    get_filename_component(dll_dir "${dll}" DIRECTORY)
    get_filename_component(dll_name "${dll}" NAME)
    if(NOT dll_dir STREQUAL EXE_DIR)
        file(COPY_FILE "${dll}" "${EXE_DIR}/${dll_name}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

if(missing)
    message(WARNING "No DLL found for: ${missing}")
endif()
