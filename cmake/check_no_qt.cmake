# Fails if the shared library given as -DLIBRARY=<path> depends on Qt, either
# directly (DT_NEEDED) or through anything it pulls in (ldd).
#
#   cmake -DLIBRARY=build/libautoremesher_core.so -P cmake/check_no_qt.cmake

if (NOT LIBRARY OR NOT EXISTS "${LIBRARY}")
  message(FATAL_ERROR "LIBRARY not set or missing: '${LIBRARY}'")
endif()

execute_process(COMMAND ldd "${LIBRARY}"
  OUTPUT_VARIABLE ldd_out
  ERROR_VARIABLE ldd_err
  RESULT_VARIABLE ldd_rv)
if (NOT ldd_rv EQUAL 0)
  message(FATAL_ERROR "ldd failed (${ldd_rv}): ${ldd_err}")
endif()
message(STATUS "ldd ${LIBRARY}:\n${ldd_out}")

string(TOLOWER "${ldd_out}" ldd_lower)
if (ldd_lower MATCHES "qt")
  message(FATAL_ERROR "${LIBRARY} depends on Qt")
endif()
if (ldd_lower MATCHES "not found")
  message(FATAL_ERROR "${LIBRARY} has unresolved shared library dependencies")
endif()
