project(kv-unit-tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_kv_server_linker_option(KV_SERVER_LDFLAGS)

# Build GoogleTest-based tests
message(STATUS "Building gtest unit tests")
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

# Link with GoogleTest using target names
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

# Enable testing and discover tests
enable_testing()
include(GoogleTest)
gtest_discover_tests(valkey-unit-gtests)

# Add custom test target using gtest-parallel
add_custom_target(test-unit
    COMMAND sh -c "TEST_ARGS=''; \
        [ -n \"$accurate\" ] && TEST_ARGS=\"$TEST_ARGS --accurate\"; \
        [ -n \"$large_memory\" ] && TEST_ARGS=\"$TEST_ARGS --large-memory\"; \
        [ -n \"$valgrind\" ] && TEST_ARGS=\"$TEST_ARGS --valgrind\"; \
        [ -n \"$seed\" ] && TEST_ARGS=\"$TEST_ARGS --seed $seed\"; \
        python3 ${CMAKE_SOURCE_DIR}/deps/gtest-parallel/gtest_parallel.py $<TARGET_FILE:valkey-unit-gtests> --gtest_filter=\"$UNIT_TEST_PATTERN\"* -- $TEST_ARGS"
    DEPENDS valkey-unit-gtests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running tests with gtest-parallel"
    VERBATIM
)
