set(DEPS_DIR "${CMAKE_BINARY_DIR}/_deps")

function(ciniva_fetch name url sha256 out_file)
    get_filename_component(file_name "${url}" NAME)
    set(dest "${DEPS_DIR}/${file_name}")
    if(NOT EXISTS "${dest}")
        message(STATUS "Downloading ${name}")
        file(DOWNLOAD "${url}" "${dest}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS st)
        list(GET st 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${dest}")
            message(FATAL_ERROR "Failed to download ${name}: ${st}")
        endif()
    endif()
    file(SHA256 "${dest}" actual)
    if(NOT actual STREQUAL sha256)
        file(REMOVE "${dest}")
        message(FATAL_ERROR "${name} hash mismatch: ${actual}")
    endif()
    set(${out_file} "${dest}" PARENT_SCOPE)
endfunction()

set(MPV_TAG 20261107)
set(MPV_ARCHIVE mpv-dev-x86_64-v3-20261107-git-9028187538.7z)
ciniva_fetch(libmpv
    "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/${MPV_TAG}/${MPV_ARCHIVE}"
    6760e5e4659d241778687a8cf997ffa7155b32cfe0e2e566728d7fc84cecdcb6 mpv_archive)
set(MPV_DIR "${DEPS_DIR}/mpv-${MPV_TAG}")
if(NOT EXISTS "${MPV_DIR}/libmpv-2.dll")
    file(ARCHIVE_EXTRACT INPUT "${mpv_archive}" DESTINATION "${MPV_DIR}")
endif()
set(MPV_INCLUDE_DIR "${MPV_DIR}/include")
set(MPV_DLL "${MPV_DIR}/libmpv-2.dll")

ciniva_fetch(nlohmann_json
    "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp"
    aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 json_hpp)
file(COPY "${json_hpp}" DESTINATION "${DEPS_DIR}/json/nlohmann")
set(JSON_INCLUDE_DIR "${DEPS_DIR}/json")

ciniva_fetch(monocypher
    "https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz"
    8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66 monocypher_archive)
set(MONOCYPHER_DIR "${DEPS_DIR}/monocypher-4.0.3")
if(NOT EXISTS "${MONOCYPHER_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${monocypher_archive}" DESTINATION "${DEPS_DIR}")
endif()
add_library(monocypher STATIC "${MONOCYPHER_DIR}/src/monocypher.c")
target_include_directories(monocypher PUBLIC "${MONOCYPHER_DIR}/src")
