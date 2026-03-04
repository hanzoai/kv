project(kvlua)

if (KV_DEBUG_BUILD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -fno-common -g -ggdb -std=c99 -O2 -D_GNU_SOURCE")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -fno-common -O3 -std=c99  -D_GNU_SOURCE")
endif()

set(LUA_ENGINE_SRCS
    engine_lua.c
    script_lua.c
    function_lua.c
    debug_lua.c
    list.c
    ../../sha1.c
    ../../rand.c)

add_library(kvlua SHARED "${LUA_ENGINE_SRCS}")

target_link_libraries(kvlua PRIVATE lualib fpconv)
target_include_directories(kvlua PRIVATE ../../../deps/lua/src)

install(TARGETS kvlua
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
