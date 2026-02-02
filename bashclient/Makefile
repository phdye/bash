# Makefile for bashclient
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
RM = rm -f

EXEEXT = .exe
OBJEXT = o

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin

srcdir = .

topdir = ..
BUILD_DIR = /home/phdyex/my-repos/cygwin/bash

DESTDIR =

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}

PROFILE_FLAGS = 
CFLAGS = -g -O2 -Wno-parentheses -Wno-format-security
CPPFLAGS = -I/home/phdyex/include
LOCAL_CFLAGS = -DRECYCLES_PIDS
DEFS = -DHAVE_CONFIG_H
LOCAL_DEFS = -DSHELL

LDFLAGS = 
LOCAL_LDFLAGS = 
LIBS = -ldl 

INCLUDES = -I. -I$(topdir)

BASE_CCFLAGS = ${PROFILE_FLAGS} $(DEFS) $(LOCAL_DEFS) \
	${INCLUDES} $(LOCAL_CFLAGS)

CCFLAGS = $(BASE_CCFLAGS) $(CPPFLAGS) $(CFLAGS)

# Program name
Program = bashclient$(EXEEXT)

# Source files
SOURCES = bashclient.c

# Object files
OBJECTS = bashclient.$(OBJEXT)

.c.$(OBJEXT):
	$(RM) $@
	$(CC) $(CCFLAGS) -c $<

all: $(Program)

$(Program): $(OBJECTS)
	$(RM) $@
	$(CC) $(LDFLAGS) $(LOCAL_LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

install: $(Program)
	$(INSTALL_PROGRAM) $(Program) $(DESTDIR)$(bindir)/$(Program)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(Program)

clean:
	$(RM) $(Program) $(OBJECTS)

distclean: clean
	$(RM) Makefile

mostlyclean: clean

maintainer-clean: distclean

# Dependencies
bashclient.$(OBJEXT): bashclient.c

.PHONY: all install uninstall clean distclean mostlyclean maintainer-clean
