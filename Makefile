# Makefile for the Z80 assembler by shevek
# Copyright 2002-2007  Bas Wijnen
#
# This file is part of z80asm.
#
# Z80asm is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Z80asm is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

CC = gcc
CFLAGS = -O0 -Wall -Wwrite-strings -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -pedantic -ansi -Wshadow -ggdb3 -W -Ignulib
SHELL = /bin/bash
VERSION ?= $(shell echo -n `cat VERSION | cut -d. -f1`. ; echo $$[`cat VERSION | cut -d. -f2` + 1])

all:z80asm

z80asm: z80asm.o expressions.o Makefile gnulib/getopt.o gnulib/getopt1.o
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
	$(MAKE) -C tests || rm $@

%.o:%.c z80asm.h gnulib/getopt.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@ -DVERSION=\"$(shell cat VERSION)\"

clean:
	for i in . gnulib examples headers ; do \
		rm -f $$i/core $$i/*~ $$i/\#* $$i/*.o $$i/*.rom ; \
	done
	rm -f z80asm z80asm.exe

dist: clean
	! git status | grep modified
	echo $(VERSION) > VERSION
	git add VERSION
	-git commit -m "Release version $(VERSION)"
	rm -rf /tmp/z80asm-$(VERSION)
	git archive --format=tar --prefix=z80asm-$(VERSION)/ HEAD | tar xf - -C /tmp
	tar cvzf ../z80asm-$(VERSION).tar.gz -C /tmp z80asm-$(VERSION)
	rm -r /tmp/z80asm-$(VERSION)
	cd .. && gpg -b z80asm-$(VERSION).tar.gz
	scp ../z80asm-$(VERSION).tar.gz* dl.sv.nongnu.org:/releases/z80asm/
	git push
