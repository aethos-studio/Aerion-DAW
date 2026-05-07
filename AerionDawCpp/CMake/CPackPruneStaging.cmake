# CPack pre-build script.
#
# Runs after all install() rules have staged files into
# CPACK_TEMPORARY_DIRECTORY, but before the actual installer (NSIS / DragNDrop)
# is generated. Used to drop the JUCE / Tracktion / juceaide leftovers that
# their FetchContent CMakeLists.txt files install unconditionally.
#
# Anything we touch here is staging-only; the build tree is unaffected.

if (NOT DEFINED CPACK_TEMPORARY_DIRECTORY)
    return()
endif()

set(_aerion_pkg_root "${CPACK_TEMPORARY_DIRECTORY}")

set(_aerion_prune_dirs
    "${_aerion_pkg_root}/bin/JUCE-8.0.6"        # juceaide.exe
    "${_aerion_pkg_root}/bin"                     # empty after juceaide removal
    "${_aerion_pkg_root}/include"                 # JUCE + Tracktion module headers
    "${_aerion_pkg_root}/lib/JUCE-8.0.6"          # exported juce_*.lib files
    "${_aerion_pkg_root}/lib/cmake"               # JUCE / Tracktion CMake exports
    "${_aerion_pkg_root}/share"                   # cmake config exports
    "${_aerion_pkg_root}/Modules"                 # JUCE module sources
)

foreach (_d IN LISTS _aerion_prune_dirs)
    if (EXISTS "${_d}")
        file(REMOVE_RECURSE "${_d}")
        message(STATUS "Aerion DAW packaging: pruned ${_d}")
    endif()
endforeach()

# After removing those buckets, lib/ might still be empty; clean it up too.
if (EXISTS "${_aerion_pkg_root}/lib")
    file(GLOB _lib_remaining "${_aerion_pkg_root}/lib/*")
    if (NOT _lib_remaining)
        file(REMOVE_RECURSE "${_aerion_pkg_root}/lib")
    endif()
endif()
