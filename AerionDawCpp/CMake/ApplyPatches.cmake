# Applies Aerion's patches to a third-party source tree at configure time.
#
# Works on whichever tree FetchContent uses, including a local checkout set
# with FETCHCONTENT_SOURCE_DIR_<NAME> (where FetchContent's own PATCH_COMMAND
# never runs). Each patch is applied once: a patch that is already in the tree
# is skipped, and one that neither applies nor is already there stops the
# configure, because the build would silently lose the change.

find_package(Git REQUIRED)

function(aerion_apply_patches sourceDir patchDir)
    file(GLOB patches "${patchDir}/*.patch")
    list(SORT patches)

    foreach(patch IN LISTS patches)
        get_filename_component(patchName "${patch}" NAME)

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --check --reverse --ignore-whitespace "${patch}"
            WORKING_DIRECTORY "${sourceDir}"
            RESULT_VARIABLE alreadyApplied
            OUTPUT_QUIET ERROR_QUIET)

        if(alreadyApplied EQUAL 0)
            continue()
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch}"
            WORKING_DIRECTORY "${sourceDir}"
            RESULT_VARIABLE applyResult
            ERROR_VARIABLE applyError)

        if(NOT applyResult EQUAL 0)
            message(FATAL_ERROR "Could not apply ${patchName} to ${sourceDir}:\n${applyError}")
        endif()

        message(STATUS "Applied ${patchName}")
    endforeach()
endfunction()
