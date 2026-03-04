project(kv-server)

set(INSTALL_BIN_PATH ${CMAKE_INSTALL_PREFIX}/bin)
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)

# Target: kv-server
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${KV_SERVER_CFLAGS}")
message(STATUS "CFLAGS: ${CMAKE_C_FLAGS}")

get_kv_server_linker_option(KV_SERVER_LDFLAGS)
list(APPEND SERVER_LIBS "fpconv")
list(APPEND SERVER_LIBS "hdr_histogram")
kv_build_and_install_bin(kv-server "${KV_SERVER_SRCS}" "${KV_SERVER_LDFLAGS}" "${SERVER_LIBS}"
                             "redis-server")
add_dependencies(kv-server generate_commands_def)
add_dependencies(kv-server generate_fmtargs_h)
add_dependencies(kv-server release_header)

if (BUILD_LUA)
    message(STATUS "Build Lua scripting engine module")
    add_subdirectory(modules/lua)
    add_dependencies(kv-server kvlua)
    target_compile_definitions(kv-server PRIVATE LUA_ENABLED)
    if (UNIX AND NOT APPLE)
        target_compile_definitions(kv-server PRIVATE LUA_LIB=libkvlua.so)
        target_link_options(kv-server PRIVATE -Wl,--disable-new-dtags)
    else ()
        target_compile_definitions(kv-server PRIVATE LUA_LIB=libkvlua.dylib)
    endif ()
    set(KV_INSTALL_RPATH "")
    set_target_properties(kv-server PROPERTIES
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR};${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()
unset(BUILD_LUA CACHE)

if (KV_RELEASE_BUILD)
    # Enable LTO for Release build
    set_property(TARGET kv-server PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

if (DEBUG_FORCE_DEFRAG)
    message(STATUS "Forcing Active Defrag run on kv-server")
    target_compile_definitions(kv-server PRIVATE DEBUG_FORCE_DEFRAG)
    target_compile_definitions(kv-server PRIVATE HAVE_DEFRAG)
endif ()

if (BUILD_SANITIZER)
    # 'BUILD_SANITIZER' is defined in KVSetup module (based on user input)
    # If defined, the variables 'KV_SANITAIZER_CFLAGS' and 'KV_SANITAIZER_LDFLAGS'
    # are set with the link & compile flags required
    message(STATUS "Adding sanitizer flags for target kv-server")
    target_compile_options(kv-server PRIVATE ${KV_SANITAIZER_CFLAGS})
    target_link_options(kv-server PRIVATE ${KV_SANITAIZER_LDFLAGS})
endif ()
unset(BUILD_SANITIZER CACHE)

# Target: kv-cli
list(APPEND CLI_LIBS "fpconv")
list(APPEND CLI_LIBS "linenoise")
kv_build_and_install_bin(kv-cli "${KV_CLI_SRCS}" "${KV_SERVER_LDFLAGS}" "${CLI_LIBS}" "redis-cli")
add_dependencies(kv-cli generate_commands_def)
add_dependencies(kv-cli generate_fmtargs_h)

# Target: kv-benchmark
list(APPEND BENCH_LIBS "fpconv")
list(APPEND BENCH_LIBS "hdr_histogram")
kv_build_and_install_bin(kv-benchmark "${KV_BENCHMARK_SRCS}" "${KV_SERVER_LDFLAGS}" "${BENCH_LIBS}"
                             "redis-benchmark")
add_dependencies(kv-benchmark generate_commands_def)
add_dependencies(kv-benchmark generate_fmtargs_h)

# Targets: kv-sentinel, kv-check-aof and kv-check-rdb are just symbolic links
kv_create_symlink("kv-server" "kv-sentinel")
kv_create_symlink("kv-server" "kv-check-rdb")
kv_create_symlink("kv-server" "kv-check-aof")

# Target kv-rdma
if (BUILD_RDMA_MODULE)
    set(MODULE_NAME "kv-rdma")
    message(STATUS "Building RDMA module")
    add_library(${MODULE_NAME} SHARED "${KV_RDMA_MODULE_SRCS}")
    target_compile_options(${MODULE_NAME} PRIVATE -DBUILD_RDMA_MODULE=2 -DUSE_RDMA=1)
    target_link_libraries(${MODULE_NAME} "${RDMA_LIBS}")
    # remove the "lib" prefix from the module
    set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")
    kv_install_bin(${MODULE_NAME})
endif ()

# Target kv-tls (a module)
if (BUILD_TLS_MODULE)
    message(STATUS "Building TLS as a module")
    set(MODULE_NAME "kv-tls")
    add_library(${MODULE_NAME} SHARED ${KV_TLS_MODULE_SRCS})
    target_compile_options(${MODULE_NAME} PRIVATE -DUSE_OPENSSL=2 -DBUILD_TLS_MODULE=2)
    if (APPLE)
        # Some symbols can only be resolved during runtime (they exist in the executable)
        target_link_options(${MODULE_NAME} PRIVATE -undefined dynamic_lookup)
    endif ()
    target_link_libraries(${MODULE_NAME} kv::kv_tls OpenSSL::SSL)
    set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")
endif ()

if (BUILD_EXAMPLE_MODULES)
    # Include the modules ("hello*")
    message(STATUS "Building example modules")
    add_subdirectory(modules)
endif ()

if (BUILD_UNIT_TESTS)
    add_subdirectory(unit)
endif ()

# Friendly hint like the Makefile one
file(RELATIVE_PATH _CMAKE_DIR_RELATIVE_PATH "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
add_custom_target(hint ALL
    DEPENDS kv-server kv-cli kv-benchmark
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Hint: It is a good idea to run tests with your CMake-built binaries \\;\\)"
    COMMAND ${CMAKE_COMMAND} -E echo "      ./${_CMAKE_DIR_RELATIVE_PATH}/runtest"
    COMMAND ${CMAKE_COMMAND} -E echo ""
)