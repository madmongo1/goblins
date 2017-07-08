cmake_minimum_required(VERSION 3.7)

include("cmake/HunterGate.cmake")
include("cmake/all_functions.cmake")

HunterGate(
        URL "https://github.com/ruslo/hunter/archive/v0.18.15.tar.gz"
        SHA1 "57991b41173c0e3c7501c7929cad8323277fab4c"
        LOCAL
)

project(tiggle)
set(CMAKE_CXX_STANDARD 14)

hunter_add_package(Sugar)
include(${SUGAR_ROOT}/cmake/Sugar)
include(sugar_include)
include(sugar_files)
include(sugar_doxygen_generate)

hunter_add_package(Boost COMPONENTS system thread)
find_package(Boost REQUIRED COMPONENTS system thread)

find_package(Doxygen QUIET)

sugar_include(src)

add_executable(tiggle ${SOURCE_FILES})
target_link_libraries(tiggle Boost::system Boost::thread)
target_include_directories(tiggle PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (DOXYGEN_FOUND)
    sugar_doxygen_generate(
            DOXYFILE ${SUGAR_ROOT}/examples/Doxyfile.in
            TARGET tiggle
            DOXYTARGET tiggle-docs
            DEVELOPER)

    add_custom_target(build-the-docs ALL DEPENDS tiggle-docs)
endif ()