cmake_minimum_required(VERSION 3.25)
project(arrowlane_cpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AL_BUILD_TESTS "Build the test suite" ON)
option(AL_BUILD_ZSTD "Build the ipc-zstd lane" ON)
option(AL_SANITIZE "Enable address and undefined sanitizers" OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(GNUInstallDirs)
include(cmake/warnings.cmake)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(AL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
if(NOT AL_BUILD_TYPE)
  set(AL_BUILD_TYPE "unknown")
endif()

if(AL_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/costmodel.json" AL_COST_MODEL_JSON)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/costmodel_data.cpp.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/costmodel_data.cpp" @ONLY)

file(GLOB AL_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM AL_CORE_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/cabi.cpp")
# The glob is intentional: build.cmd reconfigures, so a new src/*.cpp is picked
# up on the next build without editing this file.
list(APPEND AL_CORE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/costmodel_data.cpp")

add_library(arrowlane_core STATIC ${AL_CORE_SOURCES})

# One translation unit is compiled for AVX2: the eight stream checksum. MSVC has
# no per function target attribute, so the file boundary is the guard. Nothing
# in it runs until crc32_have_vpclmul() returns true.
if(MSVC)
  set_source_files_properties(src/crc32_vpclmul.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
endif()
# The ARMv8 checksum calls __crc32d and __crc32b, which GCC and Clang refuse to
# inline unless the crc extension is enabled for that translation unit. The file
# boundary is the guard, as it is for the AVX2 file above.
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set_source_files_properties(src/crc32_arm.cpp PROPERTIES COMPILE_OPTIONS "-march=armv8-a+crc")
endif()
# The C ABI ships as a shared library, and a shared object cannot link a static
# archive that was compiled without position independent code. MSVC ignores
# this property; GCC and Clang need it.
set_target_properties(arrowlane_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(arrowlane_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(arrowlane_core PUBLIC cxx_std_23)
target_compile_definitions(arrowlane_core PUBLIC AL_BUILD_TYPE="${AL_BUILD_TYPE}")

target_link_libraries(arrowlane_core PUBLIC al_warnings)

if(AL_BUILD_ZSTD AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/zstd/lib/zstd.h")
  message(STATUS "vendor/zstd is missing; disabling ipc-zstd for this configure")
  set(AL_BUILD_ZSTD OFF)
endif()

if(AL_BUILD_ZSTD)
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
  set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/zstd/build/cmake"
                   "${CMAKE_CURRENT_BINARY_DIR}/zstd" EXCLUDE_FROM_ALL)
  target_compile_definitions(arrowlane_core PUBLIC AL_HAVE_ZSTD=1)
  target_link_libraries(arrowlane_core PRIVATE libzstd_static)
endif()

if(WIN32)
  # GetProcessMemoryInfo, used by the bench command.
  set(AL_PLATFORM_LIBS psapi)
else()
  set(AL_PLATFORM_LIBS "")
endif()

add_executable(arrowlane-cpp src/main.cpp)
target_link_libraries(arrowlane-cpp PRIVATE arrowlane_core ${AL_PLATFORM_LIBS})

# The C ABI lives in its own shared library, so a foreign binding loads one
# file and never sees a C++ symbol.
add_library(arrowlane_capi SHARED src/cabi.cpp)
target_link_libraries(arrowlane_capi PRIVATE arrowlane_core)
target_include_directories(arrowlane_capi PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(arrowlane_capi PROPERTIES
  OUTPUT_NAME arrowlane_capi
  WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(AL_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/harness.cpp")
  enable_testing()
  add_library(al_test_main STATIC test/harness.cpp)
  target_include_directories(al_test_main PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/test"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(al_test_main PUBLIC arrowlane_core)
  add_test(NAME cli_bench COMMAND arrowlane-cpp bench --rows 100 --repeats 1)
  set_tests_properties(cli_bench PROPERTIES
                       PASS_REGULAR_EXPRESSION "lane +wire_bytes")
  add_test(NAME cli_manifest COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:arrowlane-cpp>
    -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/docs/ai/manifest.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_manifest.cmake)
  add_test(NAME cli_describe COMMAND arrowlane-cpp describe)
  set_tests_properties(cli_describe PROPERTIES
                       PASS_REGULAR_EXPRESSION "\"package\":\"arrowlane-cpp\"")
  file(GLOB AL_TEST_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/test/*_test.cpp")
  # cabi_test compiles the ABI source directly, so it does not need the DLL.
  list(FILTER AL_TEST_SOURCES EXCLUDE REGEX "cabi_test\.cpp$")
  foreach(test_source IN LISTS AL_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE al_test_main)
    target_compile_definitions(${test_name} PRIVATE
      AL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures"
      AL_DICT_OUT="${CMAKE_CURRENT_BINARY_DIR}/dict_cpp.arrows")
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Every test runs in the build directory, so a test that writes a file
    # (dictionary_test writes the interop fixture) never touches the source
    # mount, which is read only in the container.
    set_tests_properties(${test_name} PROPERTIES
                         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endforeach()
  add_executable(cabi_test test/cabi_test.cpp src/cabi.cpp)
  target_link_libraries(cabi_test PRIVATE al_test_main)
  add_test(NAME cabi_test COMMAND cabi_test)
endif()

install(TARGETS arrowlane-cpp arrowlane_core arrowlane_capi
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
              "${CMAKE_CURRENT_SOURCE_DIR}/docs/ai/manifest.json"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" OPTIONAL)
