cmake_minimum_required(VERSION 3.7.0)

macro(getDefinedVersion name)
  set(VERSION_REGEX "^#define LIBKV_${name} (.+)$")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/kv/kv.h"
    MATCHED_LINE REGEX ${VERSION_REGEX})
  string(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${MATCHED_LINE}")
endmacro()

getDefinedVersion(VERSION_MAJOR)
getDefinedVersion(VERSION_MINOR)
getDefinedVersion(VERSION_PATCH)
set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
MESSAGE("Detected version: ${VERSION}")

PROJECT(kv LANGUAGES "C" VERSION "${VERSION}")
INCLUDE(GNUInstallDirs)

OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON)
OPTION(ENABLE_TLS "Build kv_tls for TLS support" OFF)
OPTION(DISABLE_TESTS "If tests should be compiled or not" OFF)
OPTION(ENABLE_EXAMPLES "Enable building kv examples" OFF)
option(ENABLE_IPV6_TESTS "Enable IPv6 tests requiring special prerequisites" OFF)
OPTION(ENABLE_RDMA "Build kv_rdma for RDMA support" OFF)

# Libkv requires C99 (-std=c99)
SET(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
SET(CMAKE_DEBUG_POSTFIX d)

# Set target-common flags
if(NOT WIN32)
  add_compile_options(-Werror -Wall -Wextra -pedantic)
  add_compile_options(-Wstrict-prototypes -Wwrite-strings -Wno-missing-field-initializers)
else()
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
endif()

set(kv_sources
    src/adlist.c
    src/alloc.c
    src/async.c
    src/cluster.c
    src/command.c
    src/conn.c
    src/crc16.c
    src/net.c
    src/read.c
    src/sockcompat.c
    src/kv.c
    src/vkutil.c)

# Allow the libkv provided sds and dict types to be replaced by
# compatible implementations (like KV's).
# A replaced type is not included in a built archive or shared library.
if(NOT DICT_INCLUDE_DIR)
  set(kv_sources ${kv_sources} src/dict.c)
  set(DICT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
if(NOT SDS_INCLUDE_DIR)
  set(kv_sources ${kv_sources} src/sds.c)
  set(SDS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

ADD_LIBRARY(kv ${kv_sources})
ADD_LIBRARY(kv::kv ALIAS kv)
set_target_properties(kv PROPERTIES
  C_VISIBILITY_PRESET hidden
  WINDOWS_EXPORT_ALL_SYMBOLS TRUE
  SOVERSION "${VERSION_MAJOR}"
  VERSION "${VERSION}")

IF(MSVC)
    SET_TARGET_PROPERTIES(kv
        PROPERTIES COMPILE_FLAGS /Z7)
ENDIF()
IF(WIN32)
    TARGET_LINK_LIBRARIES(kv PUBLIC ws2_32 crypt32)
ELSEIF(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    TARGET_LINK_LIBRARIES(kv PUBLIC m)
ELSEIF(CMAKE_SYSTEM_NAME MATCHES "SunOS")
    TARGET_LINK_LIBRARIES(kv PUBLIC socket)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(kv
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/kv>
    PRIVATE
        $<BUILD_INTERFACE:${DICT_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${SDS_INCLUDE_DIR}>
)

CONFIGURE_FILE(kv.pc.in kv.pc @ONLY)

set(CPACK_PACKAGE_VENDOR "KV")
set(CPACK_PACKAGE_DESCRIPTION "\
Libkv is a minimalistic C client library for the KV, KeyDB, and Redis databases

It is minimalistic because it just adds minimal support for the protocol, \
but at the same time it uses a high level printf-alike API in order to make \
it much higher level than otherwise suggested by its minimal code base and the \
lack of explicit bindings for every server command.

Apart from supporting sending commands and receiving replies, it comes with a \
reply parser that is decoupled from the I/O layer. It is a stream parser designed \
for easy reusability, which can for instance be used in higher level language bindings \
for efficient reply parsing.

kv only supports the binary-safe RESP protocol, so you can use it with any Redis \
compatible server >= 1.2.0.

The library comes with multiple APIs. There is the synchronous API, the asynchronous API \
and the reply parsing API.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hanzoai/kv")
set(CPACK_PACKAGE_CONTACT "michael dot grunder at gmail dot com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

include(CPack)

INSTALL(TARGETS kv
    EXPORT kv-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

if (MSVC AND BUILD_SHARED_LIBS)
    INSTALL(FILES $<TARGET_PDB_FILE:kv>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug RelWithDebInfo)
endif()

# Install public headers
install(DIRECTORY include/kv
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "tls.h" EXCLUDE
        PATTERN "rdma.h" EXCLUDE)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

export(EXPORT kv-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/kv-targets.cmake"
    NAMESPACE kv::)

if(WIN32)
    SET(CMAKE_CONF_INSTALL_DIR share/kv)
else()
    SET(CMAKE_CONF_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/kv)
endif()
SET(INCLUDE_INSTALL_DIR include)
include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/kv-config-version.cmake"
                                 COMPATIBILITY SameMajorVersion)
configure_package_config_file(kv-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/kv-config.cmake
                              INSTALL_DESTINATION ${CMAKE_CONF_INSTALL_DIR}
                              PATH_VARS INCLUDE_INSTALL_DIR)

INSTALL(EXPORT kv-targets
        FILE kv-targets.cmake
        NAMESPACE kv::
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/kv-config-version.cmake
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})


IF(ENABLE_TLS)
    IF (NOT OPENSSL_ROOT_DIR)
        IF (APPLE)
            SET(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        ENDIF()
    ENDIF()
    FIND_PACKAGE(OpenSSL REQUIRED)
    SET(kv_tls_sources
        src/tls.c)
    ADD_LIBRARY(kv_tls ${kv_tls_sources})
    ADD_LIBRARY(kv::kv_tls ALIAS kv_tls)

    TARGET_INCLUDE_DIRECTORIES(kv_tls
        PRIVATE
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/kv>
            $<BUILD_INTERFACE:${DICT_INCLUDE_DIR}>
            $<BUILD_INTERFACE:${SDS_INCLUDE_DIR}>
    )

    IF (APPLE AND BUILD_SHARED_LIBS)
        SET_PROPERTY(TARGET kv_tls PROPERTY LINK_FLAGS "-Wl,-undefined -Wl,dynamic_lookup")
    ENDIF()

    set_target_properties(kv_tls PROPERTIES
      C_VISIBILITY_PRESET hidden
      WINDOWS_EXPORT_ALL_SYMBOLS TRUE
      SOVERSION "${VERSION_MAJOR}"
      VERSION "${VERSION}")
    IF(MSVC)
        SET_TARGET_PROPERTIES(kv_tls
            PROPERTIES COMPILE_FLAGS /Z7)
    ENDIF()
    TARGET_LINK_LIBRARIES(kv_tls PRIVATE OpenSSL::SSL)
    if(WIN32 OR CYGWIN)
        target_link_libraries(kv_tls PRIVATE kv)
    endif()
    CONFIGURE_FILE(kv_tls.pc.in kv_tls.pc @ONLY)

    INSTALL(TARGETS kv_tls
        EXPORT kv_tls-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # Install public header
    install(FILES include/kv/tls.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kv)

    if (MSVC AND BUILD_SHARED_LIBS)
        INSTALL(FILES $<TARGET_PDB_FILE:kv_tls>
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            CONFIGURATIONS Debug RelWithDebInfo)
    endif()

    INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv_tls.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    export(EXPORT kv_tls-targets
           FILE "${CMAKE_CURRENT_BINARY_DIR}/kv_tls-targets.cmake"
           NAMESPACE kv::)

    if(WIN32)
        SET(CMAKE_CONF_INSTALL_DIR share/kv_tls)
    else()
        SET(CMAKE_CONF_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/kv_tls)
    endif()
    configure_package_config_file(kv_tls-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/kv_tls-config.cmake
                                  INSTALL_DESTINATION ${CMAKE_CONF_INSTALL_DIR}
                                  PATH_VARS INCLUDE_INSTALL_DIR)

    INSTALL(EXPORT kv_tls-targets
        FILE kv_tls-targets.cmake
        NAMESPACE kv::
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})

    INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv_tls-config.cmake
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})
ENDIF()

if(ENABLE_RDMA)
    find_library(RDMACM_LIBRARIES rdmacm REQUIRED)
    find_library(IBVERBS_LIBRARIES ibverbs REQUIRED)
    set(kv_rdma_sources src/rdma.c)
    add_library(kv_rdma ${kv_rdma_sources})
    add_library(kv::kv_rdma ALIAS kv_rdma)

    target_link_libraries(kv_rdma LINK_PRIVATE ${RDMACM_LIBRARIES} ${IBVERBS_LIBRARIES})
    target_include_directories(kv_rdma
        PRIVATE
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/kv>
            $<BUILD_INTERFACE:${DICT_INCLUDE_DIR}>
            $<BUILD_INTERFACE:${SDS_INCLUDE_DIR}>
    )

    set_target_properties(kv_rdma PROPERTIES
      C_VISIBILITY_PRESET hidden
      WINDOWS_EXPORT_ALL_SYMBOLS TRUE
      SOVERSION "${VERSION_MAJOR}"
      VERSION "${VERSION}")
    configure_file(kv_rdma.pc.in kv_rdma.pc @ONLY)

    install(TARGETS kv_rdma
        EXPORT kv_rdma-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # Install public header
    install(FILES include/kv/rdma.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kv)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv_rdma.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    export(EXPORT kv_rdma-targets
           FILE "${CMAKE_CURRENT_BINARY_DIR}/kv_rdma-targets.cmake"
           NAMESPACE kv::)

    set(CMAKE_CONF_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/kv_rdma)
    configure_package_config_file(kv_rdma-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/kv_rdma-config.cmake
                                  INSTALL_DESTINATION ${CMAKE_CONF_INSTALL_DIR}
                                  PATH_VARS INCLUDE_INSTALL_DIR)

    install(EXPORT kv_rdma-targets
        FILE kv_rdma-targets.cmake
        NAMESPACE kv::
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kv_rdma-config.cmake
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})
endif()

# Add tests
if(NOT DISABLE_TESTS)
  if(BUILD_SHARED_LIBS)
    # Test using a static library since symbols are not hidden then.
    # Use same source, include dirs and dependencies as the shared library.
    add_library(kv_unittest STATIC ${kv_sources})
    get_target_property(include_directories kv::kv INCLUDE_DIRECTORIES)
    target_include_directories(kv_unittest PUBLIC ${include_directories})
    get_target_property(link_libraries kv::kv LINK_LIBRARIES)
    if(link_libraries)
      target_link_libraries(kv_unittest PUBLIC ${link_libraries})
    endif()
    # Create libkv_unittest.a in the tests directory.
    set_target_properties(kv_unittest PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
  else()
    # Target is an alias for the static library.
    add_library(kv_unittest ALIAS kv)
  endif()

  # Make sure ctest prints the output when a test fails.
  # Must be set before including CTest.
  set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")
  include(CTest)
  add_subdirectory(tests)
endif()

# Add examples
IF(ENABLE_EXAMPLES)
    ADD_SUBDIRECTORY(examples)
ENDIF(ENABLE_EXAMPLES)
