cmake_minimum_required(VERSION 3.16)
project(array_detector CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Boost.Geometry is header-only; we only need the headers (§ CLAUDE.md: Boost,
# not CGAL). find_package locates a system/Homebrew Boost; fall back to the
# well-known Homebrew include path if the CMake config isn't installed.
find_package(Boost QUIET)
if(NOT Boost_FOUND)
  foreach(hint /opt/homebrew/include /usr/local/include)
    if(EXISTS "${hint}/boost/geometry.hpp")
      set(Boost_INCLUDE_DIRS "${hint}")
      set(Boost_FOUND TRUE)
    endif()
  endforeach()
endif()
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost.Geometry headers not found (brew install boost)")
endif()
message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")

add_library(adt
  cpp/src/slab.cpp
  cpp/src/encoding.cpp
  cpp/src/runs.cpp
  cpp/src/reconcile.cpp
  cpp/src/verify.cpp
  cpp/src/detector.cpp
  cpp/src/synthetic.cpp
  cpp/src/svg.cpp
  # Phase 2 — hierarchy recovery
  cpp/src/recover.cpp
  cpp/src/nested.cpp
  cpp/src/synthetic_hier.cpp
  cpp/src/hr_svg.cpp
  cpp/src/bench.cpp)
target_include_directories(adt PUBLIC cpp/include ${Boost_INCLUDE_DIRS})
target_compile_options(adt PRIVATE -Wall -Wextra)

add_executable(demo cpp/apps/demo.cpp)
target_link_libraries(demo PRIVATE adt)

add_executable(hr_demo cpp/apps/hr_demo.cpp)
target_link_libraries(hr_demo PRIVATE adt)

add_executable(unify_demo cpp/apps/unify_demo.cpp)
target_link_libraries(unify_demo PRIVATE adt)

add_executable(erasure_demo cpp/apps/erasure_demo.cpp)
target_link_libraries(erasure_demo PRIVATE adt)

add_executable(nested_demo cpp/apps/nested_demo.cpp)
target_link_libraries(nested_demo PRIVATE adt)

add_executable(gds_recover cpp/apps/gds_recover.cpp)
target_link_libraries(gds_recover PRIVATE adt)

add_executable(dropoff_demo cpp/apps/dropoff_demo.cpp)
target_link_libraries(dropoff_demo PRIVATE adt)

enable_testing()
add_executable(test_bugs cpp/tests/test_bugs.cpp)
target_link_libraries(test_bugs PRIVATE adt)
add_test(NAME bugs COMMAND test_bugs)

add_executable(test_hierarchy cpp/tests/test_hierarchy.cpp)
target_link_libraries(test_hierarchy PRIVATE adt)
add_test(NAME hierarchy COMMAND test_hierarchy)

add_executable(test_unify cpp/tests/test_unify.cpp)
target_link_libraries(test_unify PRIVATE adt)
add_test(NAME unify COMMAND test_unify)

add_executable(test_bench cpp/tests/test_bench.cpp)
target_link_libraries(test_bench PRIVATE adt)
add_test(NAME bench COMMAND test_bench)

add_executable(test_nested cpp/tests/test_nested.cpp)
target_link_libraries(test_nested PRIVATE adt)
add_test(NAME nested COMMAND test_nested)
