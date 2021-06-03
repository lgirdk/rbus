#############################################################################
# If not stated otherwise in this file or this component's Licenses.txt file
# the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#############################################################################

cmake_minimum_required (VERSION 2.8.7)

if (POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
  project(rbus VERSION 2.0.0)
else ()
  project(rbus)
  set(PROJECT_VERSION "2.0.0")
endif (POLICY CMP0048)

set(CMAKE_BUILD_TYPE Debug)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

enable_testing()

find_package(PkgConfig REQUIRED)
find_package(rbus-core REQUIRED)
find_package(rtMessage REQUIRED)
find_package(linenoise REQUIRED)

if (BUILD_FOR_DESKTOP)
  message("Building for desktop")
  include_directories(${CMAKE_INSTALL_PREFIX}/include)
endif (BUILD_FOR_DESKTOP)

if (CMAKE_VERSION VERSION_LESS "3.1")
  message("CMAKE ${CMAKE_CXX_COMPILER_ID}")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
      set (CMAKE_C_FLAGS "-std=c11 ${CMAKE_C_FLAGS}")
  endif ()
else ()
  set (CMAKE_CXX_STANDARD 11)
  set (CMAKE_C_STANDARD 11)
endif ()

set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Wno-type-limits -g -fno-inline")
set(CMAKE_C_FLAGS_RELEASE "-Wall -Wextra")

if (ENABLE_CODE_COVERAGE)
    include (CodeCoverage)
    if (NOT BUILD_FOR_DESKTOP)
        string(APPEND COVERAGE_COMPILER_FLAGS " -fprofile-generate=/tmp/")
    endif()
    append_coverage_compiler_flags()
    set(COVERAGE_EXCLUDES
      "${CMAKE_CURRENT_BINARY_DIR}/unittests/gtest/*"
      "test/*"
      "sampleapps/*")
    option (ENABLE_UNIT_TESTING "ENABLE_UNIT_TESTING" ON)
    if (BUILD_FOR_DESKTOP)
        if (ENABLE_CODE_COVERAGE_GOVR)
            setup_target_for_coverage_gcovr_html(NAME "${PROJECT_NAME}CodeCoverageUI")
            setup_target_for_coverage_gcovr_xml(NAME "${PROJECT_NAME}CodeCoverage")
        else ()
            setup_target_for_coverage_lcov(NAME "${PROJECT_NAME}CodeCoverageUI")
        endif()
    endif()
endif()

if (ENABLE_STATIC_CODE_ANALYZER)
    find_program(CLANG_TIDY_EXE run-clang-tidy REQUIRED)
               set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    #set(CLANG_TIDY_CHECKS "-checks=-*,modernize-*,performance-*,bugprone-*,clang-diagnostic-*,clang-analyzer-*")
    set(CLANG_TIDY_CHECKS "-checks=-google-*,-llvm-*,bugprone-*,cert-*,modernize-*,clang-diagnostic-*")
    add_custom_target(
      code-analysis
      ALL
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMAND ${CLANG_TIDY_EXE} ${CLANG_TIDY_CHECKS} 2>&1 > ${CMAKE_CURRENT_BINARY_DIR}/clang-tidy-output.txt
                 DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json)
endif()

option(BUILD_RBUS_INTERFACE_SAMPLE_APPS "BUILD_RBUS_INTERFACE_SAMPLE_APPS" ON)
option(BUILD_RBUS_INTERFACE_TEST_APPS "BUILD_RBUS_INTERFACE_TEST_APPS" ON)
option(BUILD_RBUS_INTERFACE_UTIL_APPS "BUILD_RBUS_INTERFACE_UTIL_APPS" ON)
option(ENABLE_RDKLOGGER "ENABLE_RDKLOGGER" OFF)

if (ENABLE_RDKLOGGER)
    find_package(rdklogger REQUIRED)
    add_definitions(-DENABLE_RDKLOGGER)
endif (ENABLE_RDKLOGGER)


add_subdirectory(src)

if(BUILD_RBUS_INTERFACE_SAMPLE_APPS)
    add_subdirectory(sampleapps)
endif()

if(BUILD_RBUS_INTERFACE_TEST_APPS)
    add_subdirectory(test)
endif()

if(BUILD_RBUS_INTERFACE_UTIL_APPS)
    add_subdirectory(util)
endif()

if (ENABLE_UNIT_TESTING)
    add_subdirectory(unittests)
endif()
