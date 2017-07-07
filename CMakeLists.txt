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
include (${SUGAR_ROOT}/cmake/Sugar)
include (sugar_include)
include (sugar_files)

hunter_add_package(Boost COMPONENTS system)
find_package(Boost REQUIRED COMPONENTS system)

sugar_files(SOURCE_FILES config.hpp main.cpp run_pool.hpp worker_thread_service.hpp)

add_executable(tiggle ${SOURCE_FILES})
target_link_libraries(tiggle Boost::system)
