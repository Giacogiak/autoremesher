# GUI-free build of the AutoRemesher algorithm as a shared library
# (autoremesher_core). Wholly separate from, and additive to, the qmake GUI
# build in autoremesher.pro, which this file does not replace or touch.
#
# Linux only for now (decision D3). TBB is built from the vendored
# thirdparty/tbb source and linked statically; no system TBB is used.

# 3.13 for target_link_options.
cmake_minimum_required(VERSION 3.13...3.31)
# C is only for tests/test_core_api.c, which checks the API header is valid C.
project(AutoRemesherCore C CXX)

# The vendored TBB's CMakeLists.txt requests policy version 2.8, which
# CMake >= 4.0 rejects unless a minimum policy version is supplied.
if (NOT CMAKE_VERSION VERSION_LESS 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

# Matches CONFIG += c++14 in autoremesher.pro.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GCC >= 13 rejects the bundled TBB's headers (tbb/task.h) by default.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-Wno-changes-meaning" HAS_WNO_CHANGES_MEANING)
  if (HAS_WNO_CHANGES_MEANING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-changes-meaning")
  endif()
endif()

# Build TBB from the vendored source, static only.
set(TBB_BUILD_STATIC ON CACHE BOOL " " FORCE)
set(TBB_BUILD_SHARED OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC_PROXY OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TESTS OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tbb ext_build/tbb)
# tbb_static is linked into the autoremesher_core shared library
set_property(TARGET tbb_static PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(autoremesher_core SHARED
  src/AutoRemesher/autoremesher.cpp
  src/AutoRemesher/isotropicremesher.cpp
  src/AutoRemesher/parameterizer.cpp
  src/AutoRemesher/surfacemesh.cpp
  src/AutoRemesher/singularitysimplifier.cpp
  src/AutoRemesher/constrainedleastsquares.cpp
  src/AutoRemesher/mixedintegerleastsquares.cpp
  src/AutoRemesher/framefield.cpp
  src/AutoRemesher/quadparameterizer.cpp
  src/AutoRemesher/quadextractor.cpp
  src/AutoRemesher/positionkey.cpp
  src/AutoRemesher/meshseparator.cpp
  thirdparty/isotropicremesher/isotropicremesher.cpp
  thirdparty/isotropicremesher/isotropichalfedgemesh.cpp
  thirdparty/isotropicremesher/axisalignedboundingboxtree.cpp
  thirdparty/meshoptimizer/src/simplifier.cpp
  thirdparty/meshoptimizer/src/indexgenerator.cpp
  src/autoremesher_core_api.cpp
  src/weld.cpp
)

target_include_directories(autoremesher_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/isotropicremesher
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/meshoptimizer/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tbb/include
)

# Same as autoremesher.pro (no-ops on Linux, kept for parity).
target_compile_definitions(autoremesher_core PRIVATE _USE_MATH_DEFINES NOMINMAX)
# Selects dllexport over dllimport in autoremesher_core_api.h (Windows only).
target_compile_definitions(autoremesher_core PRIVATE AUTOREMESHER_CORE_BUILDING)

# tbb_static does not declare its own pthread/dl dependencies (only the shared
# tbb target does), so they are linked here. autoremesher.pro's unix:!macx block
# also lists -lz, but nothing in this library (or the GUI) includes zlib, and
# the toolbox has no libz.so development symlink to link against, so it is left
# out.
find_package(Threads REQUIRED)
target_link_libraries(autoremesher_core PRIVATE tbb_static Threads::Threads ${CMAKE_DL_LIBS})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # --no-undefined makes a missing source file a link error instead of a load
  # error in whoever dlopens the library later; --exclude-libs keeps the
  # statically linked TBB symbols out of the library's dynamic symbol table.
  target_link_options(autoremesher_core PRIVATE
    -Wl,--no-undefined -Wl,--exclude-libs,ALL)
endif()

enable_testing()

# Exit criterion of M1: the library must not depend on Qt.
add_test(NAME autoremesher_core_no_qt
  COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:autoremesher_core>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_qt.cmake)

# The extern "C" API, exercised from C on a small synthetic sphere.
add_executable(test_core_api tests/test_core_api.c)
target_link_libraries(test_core_api PRIVATE autoremesher_core m)
add_test(NAME test_core_api COMMAND test_core_api)
set_tests_properties(test_core_api PROPERTIES TIMEOUT 300)

# autoremesher_core_weld() on small hand-built meshes.
add_executable(test_weld tests/test_weld.c)
target_link_libraries(test_weld PRIVATE autoremesher_core)
add_test(NAME test_weld COMMAND test_weld)

# M1's smoke test: weld and remesh the real input, source.obj, at the GUI's
# defaults. source.obj is not in git (*.obj is ignored), so the test is
# skipped, not failed, where the file is missing.
set(AUTOREMESHER_SOURCE_OBJ "${CMAKE_CURRENT_SOURCE_DIR}/source.obj" CACHE FILEPATH
  "Mesh for the test_core smoke test")
if (NOT EXISTS "${AUTOREMESHER_SOURCE_OBJ}")
  message(WARNING "${AUTOREMESHER_SOURCE_OBJ} not found; test_core will be skipped")
endif()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE autoremesher_core)
add_test(NAME test_core COMMAND test_core ${AUTOREMESHER_SOURCE_OBJ})
# One remesh of source.obj takes about 37 s in Release.
set_tests_properties(test_core PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# autoremesher-cli (M2): the GUI's headless mode as a standalone, Qt-free
# executable over the C API.
add_library(autoremesher_cli_options STATIC src/cli/cli_options.cpp)
target_include_directories(autoremesher_cli_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The OBJ reader and writer.  The writer starts its files with the GUI's own
# header lines, so it takes the GUI's product name, version and homepage from
# autoremesher.pro, as the GUI does.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/autoremesher.pro AUTOREMESHER_PRO)
string(REGEX MATCH "QMAKE_TARGET_PRODUCT = ([^\n]+)" _ "${AUTOREMESHER_PRO}")
set(AUTOREMESHER_APP_NAME "${CMAKE_MATCH_1}")
string(REGEX MATCH "HUMAN_VERSION = \"([^\"]+)\"" _ "${AUTOREMESHER_PRO}")
set(AUTOREMESHER_APP_HUMAN_VER "${CMAKE_MATCH_1}")
string(REGEX MATCH "HOMEPAGE_URL = \"([^\"]+)\"" _ "${AUTOREMESHER_PRO}")
set(AUTOREMESHER_APP_HOMEPAGE_URL "${CMAKE_MATCH_1}")
if (NOT AUTOREMESHER_APP_NAME OR NOT AUTOREMESHER_APP_HUMAN_VER OR NOT AUTOREMESHER_APP_HOMEPAGE_URL)
  message(FATAL_ERROR "cannot read the product name, version or homepage from autoremesher.pro")
endif()
add_library(autoremesher_cli_obj_io STATIC src/cli/obj_io.cpp)
target_include_directories(autoremesher_cli_obj_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# For autoremesher_core_api.h only: the reader and writer call nothing in it.
target_include_directories(autoremesher_cli_obj_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(autoremesher_cli_obj_io PUBLIC
  "AUTOREMESHER_APP_NAME=\"${AUTOREMESHER_APP_NAME}\""
  "AUTOREMESHER_APP_HUMAN_VER=\"${AUTOREMESHER_APP_HUMAN_VER}\""
  "AUTOREMESHER_APP_HOMEPAGE_URL=\"${AUTOREMESHER_APP_HOMEPAGE_URL}\"")

# The progress lines and the two reports, in the GUI headless mode's formats.
add_library(autoremesher_cli_report STATIC src/cli/report.cpp)
target_link_libraries(autoremesher_cli_report PUBLIC autoremesher_cli_options
  autoremesher_cli_obj_io)

add_executable(autoremesher-cli src/cli/main.cpp)
target_link_libraries(autoremesher-cli PRIVATE autoremesher_core autoremesher_cli_options
  autoremesher_cli_obj_io autoremesher_cli_report)

# Exit criterion of M2: the executable must not depend on Qt either.
add_test(NAME autoremesher_cli_no_qt
  COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:autoremesher-cli>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_qt.cmake)

add_executable(test_cli_options tests/test_cli_options.cpp)
target_link_libraries(test_cli_options PRIVATE autoremesher_cli_options)
add_test(NAME test_cli_options COMMAND test_cli_options)

add_executable(test_obj_io tests/test_obj_io.cpp)
target_link_libraries(test_obj_io PRIVATE autoremesher_cli_obj_io)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_obj_io_scratch)
# source.obj's own checks are skipped inside the test where the file is missing.
add_test(NAME test_obj_io
  COMMAND test_obj_io ${CMAKE_CURRENT_BINARY_DIR}/test_obj_io_scratch ${AUTOREMESHER_SOURCE_OBJ})

add_executable(test_report tests/test_report.cpp)
target_link_libraries(test_report PRIVATE autoremesher_cli_report)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_report_scratch)
add_test(NAME test_report COMMAND test_report ${CMAKE_CURRENT_BINARY_DIR}/test_report_scratch)

# The executable end to end, on a small synthetic sphere.
add_executable(test_cli tests/test_cli.cpp)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_cli_scratch)
add_test(NAME test_cli
  COMMAND test_cli $<TARGET_FILE:autoremesher-cli> ${CMAKE_CURRENT_BINARY_DIR}/test_cli_scratch)
set_tests_properties(test_cli PROPERTIES TIMEOUT 300)
