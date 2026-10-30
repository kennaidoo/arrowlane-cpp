# Fails when docs/ai/manifest.json is not exactly what this build reports.
if(NOT DEFINED BIN OR NOT DEFINED MANIFEST)
  message(FATAL_ERROR "BIN and MANIFEST are required")
endif()
execute_process(COMMAND "${BIN}" describe --manifest
                OUTPUT_VARIABLE actual
                RESULT_VARIABLE status
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "describe exited ${status}")
endif()
file(READ "${MANIFEST}" expected)
string(REPLACE "\r" "" expected "${expected}")
string(STRIP "${expected}" expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "docs/ai/manifest.json is stale. Regenerate it with: arrowlane-cpp describe --manifest > docs/ai/manifest.json")
endif()
message(STATUS "manifest matches describe --manifest")
