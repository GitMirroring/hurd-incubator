#
#   Copyright (C) 2019 Free Software Foundation, Inc.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

RUMPPATH=/usr/lib
RUMPLIBS=rump rumpuser rumpdev rumpdev_disk rumpdev_pci rumpvfs rumpdev_ahcisata
RUMPEXTRA=rumpdev_scsipi

dir := rumpdisk
makemode := utility

SRCS = main.c
target = rumpdisk
OBJS = $(SRCS:.c=.o)
HURDLIBS = machdevrump
LDLIBS += -lpthread

include ../Makeconf

rumpdisk.static: main.o
	$(CC) -static main.o -Wl,--whole-archive $(RUMPLIBS:%=$(RUMPPATH)/lib%_pic.a) ../libmachdevrump/libmachdevrump_pic.a -Wl,--no-whole-archive $(RUMPEXTRA:%=$(RUMPPATH)/lib%_pic.a) ../libtrivfs/libtrivfs_pic.a ../libshouldbeinlibc/libshouldbeinlibc_pic.a ../libports/libports_pic.a ../libiohelp/libiohelp_pic.a ../libihash/libihash_pic.a ../libfshelp/libfshelp_pic.a /usr/lib/i386-gnu/libpthread.a /usr/lib/i386-gnu/libpciaccess.a /usr/lib/i386-gnu/libdl.a -o $@
