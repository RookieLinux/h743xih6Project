# Component-level source discovery. CONFIGURE_DEPENDS makes Ninja/CLion ask
# CMake to rescan registered directories when source files are added/removed.
function(collect_source_directories output_variable)
    set(_sources)
    foreach(_directory IN LISTS ARGN)
        file(GLOB _directory_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}/*.s"
            "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}/*.S")
        list(APPEND _sources ${_directory_sources})
    endforeach()
    set(${output_variable} ${_sources} PARENT_SCOPE)
endfunction()

set(FIRMWARE_SOURCES)
include(cmake/components/applications.cmake)
include(cmake/components/drivers.cmake)
include(cmake/components/platform.cmake)
include(cmake/components/rtthread.cmake)
include(cmake/components/lvgl.cmake)
include(cmake/components/packages.cmake)
list(REMOVE_DUPLICATES FIRMWARE_SOURCES)

