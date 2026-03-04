cmake_minimum_required(VERSION 3.7.0)
project(examples LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # This CMakeLists.txt is used standalone to build the examples.
  # Find required kv package to get include paths.
  find_package(kv REQUIRED)
  find_package(kv_tls QUIET)
else()
  # This CMakeLists.txt is included, most likely from libkv's project
  # CMakeLists.txt. Add path to enable <kv/x.h> includes.
  include_directories(${CMAKE_SOURCE_DIR}/include)
endif()

add_executable(example-blocking blocking.c)
target_link_libraries(example-blocking kv::kv)

add_executable(example-blocking-push blocking-push.c)
target_link_libraries(example-blocking-push kv::kv)

add_executable(example-cluster-simple cluster-simple.c)
target_link_libraries(example-cluster-simple kv::kv)

if(ENABLE_TLS)
  add_executable(example-blocking-tls blocking-tls.c)
  target_link_libraries(example-blocking-tls kv::kv kv::kv_tls)

  add_executable(example-cluster-tls cluster-tls.c)
  target_link_libraries(example-cluster-tls kv::kv kv::kv_tls)
endif()

# Examples using GLib
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GLIB2 IMPORTED_TARGET glib-2.0)
  if(GLIB2_FOUND)
    add_executable(example-async-glib async-glib.c)
    target_link_libraries(example-async-glib kv::kv PkgConfig::GLIB2)
  endif()
endif()

# Examples using libev
find_path(LIBEV ev.h)
if(LIBEV)
  add_executable(example-async-libev async-libev.c)
  target_link_libraries(example-async-libev kv::kv ev)
endif()

# Examples using libevent
find_path(LIBEVENT event.h)
if(LIBEVENT)
  add_executable(example-async-libevent async-libevent.c)
  target_link_libraries(example-async-libevent kv::kv event)

  add_executable(example-cluster-async cluster-async.c)
  target_link_libraries(example-cluster-async kv::kv event)

  add_executable(example-cluster-clientside-caching-async cluster-clientside-caching-async.c)
  target_link_libraries(example-cluster-clientside-caching-async kv::kv event)

  if(ENABLE_TLS)
    add_executable(example-async-libevent-tls async-libevent-tls.c)
    target_link_libraries(example-async-libevent-tls kv::kv kv::kv_tls event)

    add_executable(example-cluster-async-tls cluster-async-tls.c)
    target_link_libraries(example-cluster-async-tls kv::kv kv::kv_tls event)
  endif()
endif()

# Examples using libhv
find_path(LIBHV hv/hv.h)
if(LIBHV)
  add_executable(example-async-libhv async-libhv.c)
  target_link_libraries(example-async-libhv kv::kv hv)
endif()

# Examples using libuv
find_path(LIBUV uv.h)
if(LIBUV)
  add_executable(example-async-libuv async-libuv.c)
  target_link_libraries(example-async-libuv kv::kv uv)
endif()

# Examples using libsystemd
find_path(LIBSDEVENT systemd/sd-event.h)
if(LIBSDEVENT)
  add_executable(example-async-libsdevent async-libsdevent.c)
  target_link_libraries(example-async-libsdevent kv::kv systemd)
endif()

# Examples using the RunLoop in Apple's CoreFoundation
if(APPLE)
  find_library(CF CoreFoundation)
  add_executable(example-async-macosx async-macosx.c)
  target_link_libraries(example-async-macosx kv::kv ${CF})
endif()
