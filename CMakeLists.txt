project(ordpath)
cmake_minimum_required(VERSION 2.6)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")

#
# Ordpath library compile options. Those can be changed during
# confugure stage.
#

set(ORDPATH_SSE2_BITBUF false CACHE BOOL
    "Use SSE2 to implement a 64 bit bitbuffer.")

mark_as_advanced(ORDPATH_ALT_SSE2_BITBUF) # hide this option
set(ORDPATH_ALT_SSE2_BITBUF false CACHE BOOL
    "Use MMX/SSE2 to implement a 64 bit bitbuffer.")

set(ORDPATH_SSE2_SEARCHTREE false CACHE BOOL
    "Use SSE2 to implement the search for enclosing interval in the encoder.")

#
# Ordpath library.
#

add_library(ordpath ordpath.c)

configure_file(config.cmake config.h)

set_property(TARGET ordpath PROPERTY COMPILE_DEFINITIONS HAVE_CONFIG_H)

if (ORDPATH_SSE2_BITBUF OR ORDPATH_ALT_SSE2_BITBUF
    OR ORDPATH_SSE2_SEARCHTREE)
set_property(TARGET ordpath PROPERTY COMPILE_FLAGS -msse2)
endif()

#
# Ordpath library test utility.
#

add_executable(ordpath-test tests/ordpath-test.c)
target_link_libraries(ordpath-test ordpath rt)

#
# Tests.
#

enable_testing()
add_subdirectory(tests)
