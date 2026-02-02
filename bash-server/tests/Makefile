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

# Test binaries (unit tests — run by 'check')
TESTS = test_protocol$(EXEEXT) test_socket$(EXEEXT) test_config$(EXEEXT) \
	test_json$(EXEEXT) test_observe$(EXEEXT) test_pty$(EXEEXT) \
	test_serialize$(EXEEXT) test_debug$(EXEEXT) test_v2_dispatch$(EXEEXT) \
	test_v2_errors$(EXEEXT)

# Test utilities (not run directly by 'check', used by integration tests)
UTILS = test_client$(EXEEXT) test_session$(EXEEXT) test_winpipe$(EXEEXT) \
	test_state$(EXEEXT) test_transport$(EXEEXT)

# Server source files needed by tests
SERVER_PROTOCOL = $(serverdir)/server_protocol.c
SERVER_SOCKET = $(serverdir)/server_socket.c
SERVER_WINPIPE = $(serverdir)/server_winpipe.c
SERVER_JSON = $(serverdir)/server_json.c
SERVER_OBSERVE = $(serverdir)/server_observe.c
SERVER_PTY = $(serverdir)/server_pty.c
SERVER_SERIALIZE = $(serverdir)/cmd_serialize.c
SERVER_DEBUG = $(serverdir)/server_debug.c

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
		$(SERVER_PROTOCOL) $(SERVER_SOCKET) $(SERVER_WINPIPE) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_config.c $(SERVER_PROTOCOL) $(SERVER_SOCKET) $(SERVER_WINPIPE)

test_client$(EXEEXT): test_client.c
	$(CC) -Wall -g -o $@ test_client.c

test_session$(EXEEXT): test_session.c $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_session.c $(SERVER_PROTOCOL)

test_winpipe$(EXEEXT): test_winpipe.c $(SERVER_PROTOCOL) $(SERVER_WINPIPE) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_winpipe.c $(SERVER_PROTOCOL) $(SERVER_WINPIPE)

test_state$(EXEEXT): test_state.c $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_state.c $(SERVER_PROTOCOL)

test_transport$(EXEEXT): test_transport.c
	$(CC) $(CFLAGS) -o $@ test_transport.c

test_json$(EXEEXT): test_json.c $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_json.c $(SERVER_JSON) $(SERVER_PROTOCOL)

test_observe$(EXEEXT): test_observe.c $(SERVER_OBSERVE) $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_observe.c $(SERVER_OBSERVE) $(SERVER_JSON) $(SERVER_PROTOCOL)

test_pty$(EXEEXT): test_pty.c $(SERVER_PTY) $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_pty.c $(SERVER_PTY) $(SERVER_JSON) $(SERVER_PROTOCOL) -lutil

test_serialize$(EXEEXT): test_serialize.c $(SERVER_SERIALIZE) $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_serialize.c $(SERVER_SERIALIZE) $(SERVER_JSON) $(SERVER_PROTOCOL)

test_debug$(EXEEXT): test_debug.c $(SERVER_DEBUG) $(SERVER_SERIALIZE) $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_debug.c $(SERVER_DEBUG) $(SERVER_SERIALIZE) $(SERVER_JSON) $(SERVER_PROTOCOL)

test_v2_dispatch$(EXEEXT): test_v2_dispatch.c $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_v2_dispatch.c $(SERVER_JSON) $(SERVER_PROTOCOL)

test_v2_errors$(EXEEXT): test_v2_errors.c $(SERVER_JSON) $(SERVER_PROTOCOL) $(serverdir)/server.h
	$(CC) $(CFLAGS) -o $@ test_v2_errors.c $(SERVER_JSON) $(SERVER_PROTOCOL)

# Integration tests (requires running bash-server)
.PHONY: check-integration
check-integration: $(UTILS)
	@echo "=== Running bash-server integration tests ==="
	@chmod +x run_integration.sh
	@./run_integration.sh

clean:
	$(RM) $(TESTS) $(UTILS)
