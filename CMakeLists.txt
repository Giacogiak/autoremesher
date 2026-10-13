# GUI-free build of the AutoRemesher algorithm as a shared library
# (autoremesher_core). Wholly separate from, and additive to, the qmake GUI
# build in autoremesher.pro, which this file does not replace or touch.
#
# Linux only for now (decision D3). TBB is built from the vendored
# thirdparty/tbb source and linked statically; no system TBB is used.

# 3.13 for target_link_options.
cmake_minimum_required(VERSION 3.13...3.31)
project(AutoRemesherCore CXX)

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
