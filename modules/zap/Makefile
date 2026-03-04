# ZAP module for hanzo/kv
# Build: make
# Load: MODULE LOAD ./zap.so [PORT 9653]

CC ?= gcc
CFLAGS = -fPIC -O2 -std=c11 -Wall -Wextra -I../../src
LDFLAGS = -shared -lpthread

.PHONY: all clean

all: zap.so

zap.so: zap_module.c zap_protocol.h
	$(CC) $(CFLAGS) -o $@ zap_module.c $(LDFLAGS)

clean:
	rm -f zap.so
