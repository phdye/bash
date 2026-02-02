# Makefile for bash-server
#
# Copyright (C) 2026 Free Software Foundation, Inc.

#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.

#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.

#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

SHELL = /bin/sh
CC = gcc
AR = ar
ARFLAGS = cr
RANLIB = ranlib
RM = rm -f
CP = cp

EXEEXT = .exe
OBJEXT = o

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
datarootdir = ${prefix}/share
docdir = ${datarootdir}/doc/bash-server

srcdir = .

topdir = ..
BUILD_DIR = /home/phdyex/my-repos/cygwin/bash

DESTDIR =

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644

PROFILE_FLAGS = 
CFLAGS = -g -O2 -Wno-parentheses -Wno-format-security
CPPFLAGS = -I/home/phdyex/include
LOCAL_CFLAGS = -DRECYCLES_PIDS
DEFS = -DHAVE_CONFIG_H
LOCAL_DEFS = -DSHELL

LDFLAGS = 
LOCAL_LDFLAGS = 
LIBS = -ldl 

# Cygwin DLL - bash-server links against libcygbash.dll.a
BASH_DLL = $(BUILD_DIR)/cygbash-5.1.dll

BASHINCDIR = ${topdir}/include
LIBSRC = ${topdir}/lib

INTL_INC = 

INCLUDES = -I. -I.. -I$(topdir) -I$(BASHINCDIR) -I$(LIBSRC) $(INTL_INC)

BASE_CCFLAGS = ${PROFILE_FLAGS} $(DEFS) $(LOCAL_DEFS) \
	${INCLUDES} $(LOCAL_CFLAGS)

CCFLAGS = $(BASE_CCFLAGS) $(CPPFLAGS) $(CFLAGS)

# Program name
Program = bash-server$(EXEEXT)

# Source files
SOURCES = server_main.c server_socket.c server_session.c server_protocol.c \
	server_state.c server_json.c server_observe.c server_pty.c server_winpipe.c \
	cmd_serialize.c server_debug.c

# Object files
OBJECTS = server_main.$(OBJEXT) server_socket.$(OBJEXT) \
	server_session.$(OBJEXT) server_protocol.$(OBJEXT) \
	server_state.$(OBJEXT) server_json.$(OBJEXT) server_observe.$(OBJEXT) \
	server_pty.$(OBJEXT) server_winpipe.$(OBJEXT) \
	cmd_serialize.$(OBJEXT) server_debug.$(OBJEXT)

# On Cygwin, all bash internals are in cygbash-X.Y.dll.
# bash-server links against the DLL import library (libcygbash.dll.a)
# instead of individual .o files and .a archives.

.c.$(OBJEXT):
	$(RM) $@
	$(CC) $(CCFLAGS) -c $<

all: $(Program)

$(Program): $(OBJECTS) $(BASH_DLL)
	$(RM) $@
	$(CC) $(LDFLAGS) $(LOCAL_LDFLAGS) -o $@ $(OBJECTS) -L$(BUILD_DIR) -lcygbash $(LIBS)

# AI documentation files
AI_DOC_DIR = $(topdir)/doc/server/ai
AI_DOCS = $(AI_DOC_DIR)/protocol.md $(AI_DOC_DIR)/api-reference.md \
	$(AI_DOC_DIR)/errors.md $(AI_DOC_DIR)/integration.md
AI_ALL = $(AI_DOC_DIR)/all.md

install: $(Program) install-ai-docs

install-program: $(Program)
	$(INSTALL_PROGRAM) $(Program) $(DESTDIR)$(bindir)/$(Program)

install-ai-docs: $(AI_ALL)
	$(INSTALL) -d $(DESTDIR)$(docdir)/ai
	@for f in $(AI_DOCS) $(AI_ALL); do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/ai/ ; \
	done

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(Program)
	$(RM) -r $(DESTDIR)$(docdir)/ai

# Generate concatenated AI doc (analogous to llms-full.txt)
$(AI_ALL): $(AI_DOCS)
	@echo "Generating $@"
	@{ echo "# bash-server — Complete AI Reference"; \
	   echo ""; \
	   echo "> This file concatenates all AI-focused reference documentation"; \
	   echo "> for the bash-server v1/v2 protocol. Individual files:"; \
	   echo "> protocol.md, api-reference.md, errors.md, integration.md"; \
	   echo ""; \
	   for f in $(AI_DOCS); do \
	       echo "---"; echo ""; \
	       cat $$f; echo ""; \
	   done; \
	} > $@

clean:
	$(RM) $(Program) $(OBJECTS)

distclean: clean
	$(RM) Makefile

mostlyclean: clean

maintainer-clean: distclean
	$(RM) $(AI_ALL)

# Dependencies
server_main.$(OBJEXT): server_main.c server.h $(topdir)/config.h
server_socket.$(OBJEXT): server_socket.c server.h $(topdir)/config.h
server_session.$(OBJEXT): server_session.c server.h $(topdir)/config.h \
	$(topdir)/shell.h $(topdir)/builtins/common.h
server_protocol.$(OBJEXT): server_protocol.c server.h $(topdir)/config.h
server_state.$(OBJEXT): server_state.c server.h $(topdir)/config.h \
	$(topdir)/variables.h $(topdir)/alias.h $(topdir)/trap.h
server_json.$(OBJEXT): server_json.c server.h $(topdir)/config.h \
	$(topdir)/shell.h $(topdir)/builtins/common.h
server_observe.$(OBJEXT): server_observe.c server.h $(topdir)/config.h \
	$(topdir)/command_hooks.h
server_pty.$(OBJEXT): server_pty.c server.h $(topdir)/config.h
server_winpipe.$(OBJEXT): server_winpipe.c server.h $(topdir)/config.h
cmd_serialize.$(OBJEXT): cmd_serialize.c server.h $(topdir)/config.h \
	$(topdir)/command.h $(topdir)/general.h
server_debug.$(OBJEXT): server_debug.c server.h $(topdir)/config.h \
	$(topdir)/command.h $(topdir)/command_hooks.h

.PHONY: all install install-program install-ai-docs uninstall clean distclean mostlyclean maintainer-clean
