# Makefile for bash-server unit tests
#
# Usage:
#   make           - build all test binaries
#   make check     - build and run all tests
#   make clean     - remove test binaries

SHELL = /bin/sh
CC = gcc
RM = rm -f

EXEEXT = .exe

# Paths
topdir = ../..
serverdir = ..

DEFS = -DHAVE_CONFIG_H
INCLUDES = -I$(serverdir) -I$(topdir) -I$(topdir)/include -I$(topdir)/lib
CFLAGS = -g -O2 -Wall $(DEFS) $(INCLUDES)

# Test binaries
TESTS = test_protocol$(EXEEXT) test_socket$(EXEEXT) test_config$(EXEEXT)

# Test utilities (not run directly by 'check', used by integration tests)
UTILS = test_client$(EXEEXT)

# Server source files needed by tests
SERVER_PROTOCOL = $(serverdir)/server_protocol.c
SERVER_SOCKET = $(serverdir)/server_socket.c

.PHONY: all check clean

all: $(TESTS) $(UTILS)

check: $(TESTS)
	@echo "=== Running bash-server unit tests ==="
	@failed=0; \
	for t in $(TESTS); do \
		echo ""; \
		echo "--- $$t ---"; \
		./$$t; \
		if [ $$? -ne 0 ]; then failed=$$((failed + 1)); fi; \
	done; \
	echo ""; \
	if [ $$failed -gt 0 ]; then \
		echo "=== $$failed test suite(s) FAILED ==="; \
		exit 1; \
	else \
		echo "=== All test suites PASSED ==="; \
	fi

test_protocol$(EXEEXT): test_protocol.c $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c $(SERVER_PROTOCOL)

test_socket$(EXEEXT): test_socket.c $(SERVER_SOCKET) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_socket.c $(SERVER_SOCKET)

test_config$(EXEEXT): test_config.c $(serverdir)/server_main.c \
		$(SERVER_PROTOCOL) $(SERVER_SOCKET) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_config.c $(SERVER_PROTOCOL) $(SERVER_SOCKET)

test_client$(EXEEXT): test_client.c
	$(CC) -Wall -g -o $@ test_client.c

clean:
	$(RM) $(TESTS) $(UTILS)
