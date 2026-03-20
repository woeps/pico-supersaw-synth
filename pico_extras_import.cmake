# This can be dropped into an external project to help locate the Pico Extras
# It should be include()ed prior to project()

if (DEFINED ENV{PICO_EXTRAS_PATH} AND (NOT PICO_EXTRAS_PATH))
    set(PICO_EXTRAS_PATH $ENV{PICO_EXTRAS_PATH})
    message("Using PICO_EXTRAS_PATH from environment ('${PICO_EXTRAS_PATH}')")
endif ()

if (DEFINED ENV{PICO_EXTRAS_FETCH_FROM_GIT} AND (NOT PICO_EXTRAS_FETCH_FROM_GIT))
    set(PICO_EXTRAS_FETCH_FROM_GIT $ENV{PICO_EXTRAS_FETCH_FROM_GIT})
    message("Using PICO_EXTRAS_FETCH_FROM_GIT from environment ('${PICO_EXTRAS_FETCH_FROM_GIT}')")
endif ()

if (DEFINED ENV{PICO_EXTRAS_FETCH_FROM_GIT_PATH} AND (NOT PICO_EXTRAS_FETCH_FROM_GIT_PATH))
    set(PICO_EXTRAS_FETCH_FROM_GIT_PATH $ENV{PICO_EXTRAS_FETCH_FROM_GIT_PATH})
    message("Using PICO_EXTRAS_FETCH_FROM_GIT_PATH from environment ('${PICO_EXTRAS_FETCH_FROM_GIT_PATH}')")
endif ()

if (DEFINED ENV{PICO_EXTRAS_FETCH_FROM_GIT_TAG} AND (NOT PICO_EXTRAS_FETCH_FROM_GIT_TAG))
    set(PICO_EXTRAS_FETCH_FROM_GIT_TAG $ENV{PICO_EXTRAS_FETCH_FROM_GIT_TAG})
    message("Using PICO_EXTRAS_FETCH_FROM_GIT_TAG from environment ('${PICO_EXTRAS_FETCH_FROM_GIT_TAG}')")
endif ()

if (PICO_EXTRAS_FETCH_FROM_GIT AND NOT PICO_EXTRAS_FETCH_FROM_GIT_TAG)
    set(PICO_EXTRAS_FETCH_FROM_GIT_TAG "master")
    message("Using master as default value for PICO_EXTRAS_FETCH_FROM_GIT_TAG")
endif ()

set(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" CACHE PATH "Path to the Pico Extras")
set(PICO_EXTRAS_FETCH_FROM_GIT "${PICO_EXTRAS_FETCH_FROM_GIT}" CACHE BOOL "Set to ON to fetch copy of Pico Extras from git if not otherwise locatable")
set(PICO_EXTRAS_FETCH_FROM_GIT_PATH "${PICO_EXTRAS_FETCH_FROM_GIT_PATH}" CACHE FILEPATH "location to download Extras")
set(PICO_EXTRAS_FETCH_FROM_GIT_TAG "${PICO_EXTRAS_FETCH_FROM_GIT_TAG}" CACHE FILEPATH "release tag for Extras")

if (NOT PICO_EXTRAS_PATH)
    if (PICO_EXTRAS_FETCH_FROM_GIT)
        include(FetchContent)
        set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})
        if (PICO_EXTRAS_FETCH_FROM_GIT_PATH)
            get_filename_component(FETCHCONTENT_BASE_DIR "${PICO_EXTRAS_FETCH_FROM_GIT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
        endif ()
        FetchContent_Declare(
                pico_extras
                GIT_REPOSITORY https://github.com/raspberrypi/pico-extras
                GIT_TAG ${PICO_EXTRAS_FETCH_FROM_GIT_TAG}
        )

        if (NOT pico_extras)
            message("Downloading Pico Extras")
            if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.17.0")
                FetchContent_Populate(
                        pico_extras
                        QUIET
                        GIT_REPOSITORY https://github.com/raspberrypi/pico-extras
                        GIT_TAG ${PICO_EXTRAS_FETCH_FROM_GIT_TAG}
                        GIT_SUBMODULES_RECURSE FALSE

                        SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-src
                        BINARY_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-build
                        SUBBUILD_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-subbuild
                )
            else ()
                FetchContent_Populate(
                        pico_extras
                        QUIET
                        GIT_REPOSITORY https://github.com/raspberrypi/pico-extras
                        GIT_TAG ${PICO_EXTRAS_FETCH_FROM_GIT_TAG}

                        SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-src
                        BINARY_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-build
                        SUBBUILD_DIR ${FETCHCONTENT_BASE_DIR}/pico_extras-subbuild
                )
            endif ()

            set(PICO_EXTRAS_PATH ${pico_extras_SOURCE_DIR})
        endif ()
        set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
    else ()
        message(FATAL_ERROR
                "Pico Extras location was not specified. Please set PICO_EXTRAS_PATH or set PICO_EXTRAS_FETCH_FROM_GIT to on to fetch from git."
                )
    endif ()
endif ()

get_filename_component(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_EXTRAS_PATH})
    message(FATAL_ERROR "Directory '${PICO_EXTRAS_PATH}' not found")
endif ()

set(PICO_EXTRAS_INIT_CMAKE_FILE ${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
if (NOT EXISTS ${PICO_EXTRAS_INIT_CMAKE_FILE})
    message(FATAL_ERROR "Directory '${PICO_EXTRAS_PATH}' does not appear to contain the Pico Extras")
endif ()

set(PICO_EXTRAS_PATH ${PICO_EXTRAS_PATH} CACHE PATH "Path to the Pico Extras" FORCE)

include(${PICO_EXTRAS_INIT_CMAKE_FILE})
