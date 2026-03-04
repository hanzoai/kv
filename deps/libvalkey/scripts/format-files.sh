#!/bin/sh

find examples include libkv src tests \
    \( -name '*.c' -or -name '*.cpp' -or -name '*.h' \) \
    -exec clang-format -i {} + ;
