CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -Isrc
LDFLAGS =

SRCDIR = src
INCDIR = include
TESTDIR = tests
EXDIR = examples

SRCS = $(SRCDIR)/bashclient.c $(SRCDIR)/transport.c $(SRCDIR)/protocol.c
OBJS = $(SRCS:.c=.o)

LIB_SHARED = libbashclient.so
LIB_STATIC = libbashclient.a

.PHONY: all clean check examples

all: $(LIB_SHARED) $(LIB_STATIC)

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/bashclient.h $(SRCDIR)/internal.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Tests
TEST_SRCS = $(TESTDIR)/test_protocol.c $(TESTDIR)/test_channels.c $(TESTDIR)/test_integration.c
TEST_BINS = $(TEST_SRCS:.c=)

check: $(LIB_STATIC) $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "=== Running $$t ==="; \
		timeout 30 ./$$t || echo "FAILED: $$t"; \
	done

$(TESTDIR)/test_protocol: $(TESTDIR)/test_protocol.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< -L. -lbashclient

$(TESTDIR)/test_channels: $(TESTDIR)/test_channels.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< -L. -lbashclient

$(TESTDIR)/test_integration: $(TESTDIR)/test_integration.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< -L. -lbashclient

# Examples
EXAMPLE_SRCS = $(EXDIR)/eval.c $(EXDIR)/observe.c $(EXDIR)/debugger.c $(EXDIR)/pty.c
EXAMPLE_BINS = $(EXAMPLE_SRCS:.c=)

examples: $(LIB_STATIC) $(EXAMPLE_BINS)

$(EXDIR)/%: $(EXDIR)/%.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< -L. -lbashclient

clean:
	rm -f $(OBJS) $(LIB_SHARED) $(LIB_STATIC) $(TEST_BINS) $(EXAMPLE_BINS)
