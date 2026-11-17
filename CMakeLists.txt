cmake_minimum_required(VERSION 3.25)

# Process taps, the Core Audio feature siphon uses, arrived in macOS 14.2.
# This tells the compiler and linker the oldest macOS the program must run
# on. It must be set before project(), and CMake ignores it on Linux.
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.2 CACHE STRING "Oldest macOS to support")

project(siphon LANGUAGES CXX)

# Compile as C++20, and fail rather than quietly fall back to an older
# standard if the compiler doesn't support it.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Write compile_commands.json into the build folder. Tools like clangd and
# clang-tidy read it to learn how each file is compiled.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The portable parts go into a static library, `siphon_core`: the ring
# buffer, the sample math, the WAV writer and the option parsing. None of it
# uses macOS, so it builds and its tests run on Linux too (that is what CI
# does). The program and the tests both link it, so each source file is
# compiled once and the tests check exactly the code the program runs.
add_library(siphon_core STATIC
    src/options.cpp
    src/ring_buffer.cpp
    src/sample_math.cpp
    src/wav_writer.cpp
)

# PUBLIC on an include folder means "use it to build this target, and also
# pass it on to every target that links this one". The tests need it, to
# find headers such as ring_buffer.h.
target_include_directories(siphon_core PUBLIC src)
# -Wall and -Wextra turn on most useful compiler warnings. -Wpedantic warns
# about compiler-specific extensions that aren't standard C++. PRIVATE keeps
# these flags to our own targets, so they don't leak into code that links
# the core.
target_compile_options(siphon_core PRIVATE -Wall -Wextra -Wpedantic)

# --- The program (macOS only) ---
if (APPLE)
    # OBJCXX is Objective-C++, for system_tap.mm. It is only turned on here,
    # so Linux builds don't need an Objective-C compiler.
    enable_language(OBJCXX)

    add_executable(siphon src/main.cpp src/system_tap.mm)
    # PRIVATE: nothing links against a program, so there is nothing to pass
    # on. CoreAudio has the tap and device API. Foundation has the
    # Objective-C basics (NSString, NSDictionary, NSUUID).
    target_link_libraries(siphon PRIVATE siphon_core
        "-framework CoreAudio" "-framework Foundation")
    target_compile_options(siphon PRIVATE -Wall -Wextra -Wpedantic)

    # ARC (automatic reference counting) frees Objective-C objects for us.
    # See the top of system_tap.mm.
    set_source_files_properties(src/system_tap.mm PROPERTIES
        COMPILE_OPTIONS -fobjc-arc)

    # macOS asks the user before a program may record system audio, and the
    # question shows the text in Info.plist's NSAudioCaptureUsageDescription.
    # A command-line tool has no .app folder to hold an Info.plist, so the
    # linker copies the file into the program itself, into a section named
    # __TEXT,__info_plist, where macOS looks for it.
    target_link_options(siphon PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
    # Relink when Info.plist changes. The linker reads it, but CMake doesn't
    # know that unless it is told.
    set_property(TARGET siphon APPEND PROPERTY
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)
endif()

# --- Tests ---
# enable_testing() turns on CTest, CMake's test runner. Each test is one
# program; `ctest` runs them all and reports which failed (a program fails
# by returning a non-zero exit code).
enable_testing()

# A function keeps each test to one line below. ${name} is the test's name,
# and its source file is tests/${name}.cpp.
function(siphon_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE siphon_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

siphon_test(options_test)
siphon_test(ring_buffer_test)
siphon_test(sample_math_test)
siphon_test(wav_writer_test)
