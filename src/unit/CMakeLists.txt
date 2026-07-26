project(kv-unit-tests)

file(GLOB UNIT_TEST_SRCS "${CMAKE_CURRENT_LIST_DIR}/*.c")
set(UNIT_TEST_SRCS "${UNIT_TEST_SRCS}")

get_kv_server_linker_option(KV_SERVER_LDFLAGS)

# Build unit tests only
message(STATUS "Building unit tests")
if (USE_TLS)
    if (BUILD_TLS_MODULE)
        # TLS as a module
        list(APPEND COMPILE_DEFINITIONS "USE_OPENSSL=2")
    else (BUILD_TLS_MODULE)
        # Built-in TLS support
        list(APPEND COMPILE_DEFINITIONS "USE_OPENSSL=1")
        list(APPEND COMPILE_DEFINITIONS "BUILD_TLS_MODULE=0")
    endif ()
endif ()

# Build KV sources as a static library for the test
add_library(kvlib STATIC ${KV_SERVER_SRCS})
target_compile_options(kvlib PRIVATE "${COMPILE_FLAGS}")
target_compile_definitions(kvlib PRIVATE "${COMPILE_DEFINITIONS}")

add_executable(kv-unit-tests ${UNIT_TEST_SRCS})
target_compile_options(kv-unit-tests PRIVATE "${COMPILE_FLAGS}")
target_compile_definitions(kv-unit-tests PRIVATE "${COMPILE_DEFINITIONS}")
add_dependencies(kv-unit-tests generate_test_files_h)

if (UNIX AND NOT APPLE)
    # Avoid duplicate symbols on non macOS
    target_link_options(kv-unit-tests PRIVATE "-Wl,--allow-multiple-definition")
endif ()

if (USE_JEMALLOC)
    # Using jemalloc
    target_link_libraries(kv-unit-tests jemalloc)
endif ()

if (IS_FREEBSD)
    target_link_libraries(kv-unit-tests execinfo)
endif ()

target_link_libraries(
    kv-unit-tests
    kvlib
    fpconv
    lualib
    hdr_histogram
    kv::kv
    ${KV_SERVER_LDFLAGS})

if (USE_TLS)
    # Add required libraries needed for TLS
    target_link_libraries(kv-unit-tests OpenSSL::SSL kv::kv_tls)
endif ()
