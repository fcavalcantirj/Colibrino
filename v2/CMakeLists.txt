# Colibrino v2 pure core: host-buildable C11 domain units + Unity oracle suite.
#
# Nothing here touches hardware. The ESP32-S3 firmware (../sticks3) is a
# separate PlatformIO project that will consume these headers later; the C++17
# language is enabled only for the two test targets that include the headers
# from C++ (contracts_cxx) and link the v1 detector (blink_differential).
cmake_minimum_required(VERSION 3.23)
project(colibrino_v2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# C++ is opt-in per target (see test/CMakeLists.txt); keep extensions off there too.
set(CMAKE_CXX_EXTENSIONS OFF)

# The oracle gate: golden-trace tests FAIL instead of SKIP when no fixtures are
# promoted under traces/. Developer preset leaves this OFF.
option(COLIBRINO_V2_ORACLE "Golden tests fail (not skip) without promoted fixtures" OFF)

include(cmake/warnings.cmake)
include(cmake/clang_tidy.cmake)

enable_testing()

add_subdirectory(third_party)
add_subdirectory(core)
add_subdirectory(test)
