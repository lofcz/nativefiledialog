cmake_minimum_required(VERSION 3.25)
project(nativefiledialog)

set(CMAKE_CXX_STANDARD 14)

# Nastavení výstupních cest pro různé architektury
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUFFIX "64")
else()
    set(ARCH_SUFFIX "32")
endif()

# Nastavení výstupních adresářů
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(src)
include_directories(src/include)

# Platformově závislé nastavení
if(WIN32)
    set(PLATFORM_SOURCES
            src/nfd_win.cpp
            src/nfd_common.c
    )

    set(PLATFORM_LIBS
            comctl32
            comdlg32
            ole32
            uuid
            shell32
    )

    set(PLATFORM_DEFINITIONS
            MODULE_API_EXPORTS
    )
elseif(APPLE)
    set(PLATFORM_SOURCES
            src/nfd_cocoa.m
            src/nfd_common.c
    )

    set(PLATFORM_LIBS
            "-framework AppKit"
    )

    set(PLATFORM_COMPILE_OPTIONS
            -ObjC
    )
else()
    # Linux/Unix systémy
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    include_directories(${GTK3_INCLUDE_DIRS})

    set(PLATFORM_SOURCES
            src/nfd_gtk.c
            src/nfd_common.c
    )

    set(PLATFORM_LIBS
            ${GTK3_LIBRARIES}
    )

    set(PLATFORM_COMPILE_OPTIONS
            ${GTK3_CFLAGS_OTHER}
    )
endif()

# Hlavičkové soubory
set(HEADER_FILES
        src/include/nfd.h
        src/common.h
        src/nfd_common.h
)

# Vytvoření release knihovny
add_library(nativefiledialog_release SHARED
        ${PLATFORM_SOURCES}
        ${HEADER_FILES}
)

# Nastavení vlastností release knihovny
set_target_properties(nativefiledialog_release PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PUBLIC_HEADER "${HEADER_FILES}"
        OUTPUT_NAME "nfd${ARCH_SUFFIX}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Release"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Release"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Release"
)

# Release optimalizace
target_compile_options(nativefiledialog_release PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /Ob2 /DNDEBUG>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3 -DNDEBUG>
        ${PLATFORM_COMPILE_OPTIONS}
)

# Vytvoření debug knihovny
add_library(nativefiledialog SHARED
        ${PLATFORM_SOURCES}
        ${HEADER_FILES}
)

# Nastavení vlastností debug knihovny
set_target_properties(nativefiledialog PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PUBLIC_HEADER "${HEADER_FILES}"
        OUTPUT_NAME "nfd${ARCH_SUFFIX}_d"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Debug"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Debug"
)

# Přidání platform-specific definic a knihoven
if(PLATFORM_DEFINITIONS)
    target_compile_definitions(nativefiledialog_release PRIVATE ${PLATFORM_DEFINITIONS})
    target_compile_definitions(nativefiledialog PRIVATE ${PLATFORM_DEFINITIONS})
endif()

target_link_libraries(nativefiledialog_release PRIVATE ${PLATFORM_LIBS})
target_link_libraries(nativefiledialog PRIVATE ${PLATFORM_LIBS})

if(NOT WIN32)
    target_compile_options(nativefiledialog PRIVATE ${PLATFORM_COMPILE_OPTIONS})
endif()

# Testy
set(TEST_TARGETS
        test_opendialog
        test_opendialogmultiple
        test_pickfolder
        test_savedialog
)

# Vytvoření testovacích executable
foreach(test ${TEST_TARGETS})
    # Debug verze
    add_executable(${test} test/${test}.c)
    set_target_properties(${test} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug"
    )
    target_link_libraries(${test} PRIVATE nativefiledialog)

    # Release verze
    add_executable(${test}_release test/${test}.c)
    set_target_properties(${test}_release PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Release"
    )
    target_link_libraries(${test}_release PRIVATE nativefiledialog_release)
endforeach()

# Demo aplikace
# Debug verze
add_executable(file_dialog_demo
        src/demo.cpp
)
set_target_properties(file_dialog_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug"
)
target_link_libraries(file_dialog_demo
        PRIVATE nativefiledialog
)

# Release verze
add_executable(file_dialog_demo_release
        src/demo.cpp
)
set_target_properties(file_dialog_demo_release PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Release"
)
target_link_libraries(file_dialog_demo_release
        PRIVATE nativefiledialog_release
)

# Nastavení výchozího cíle
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT file_dialog_demo)