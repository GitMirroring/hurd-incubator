# Copyright (C) 2009 Free Software Foundation, Inc.
# This file is part of the GNU Hurd.
#
# The GNU Hurd is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with the GNU Hurd; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libmachdevrump
makemode := library
libname = libmachdevrump

SRCS = deviceUser.c machUser.c ds_routines.c trivfs_server.c \
       device_replyUser.c deviceServer.c notifyServer.c block.c
#block.c
LCLHDRS = dev_hdr.h device_emul.h ds_routines.h vm_param.h \
	  io_req.h machdevrump.h errno-base.h
installhdrs = machdevrump.h
HURDLIBS = ports trivfs
LDLIBS += -lpthread
LDLIBS += -lrump -lrumpdev -lrumpdev_disk -lrumpdev_pci -lrumpvfs -lrumpdev_scsipi -lrumpdev_ahcisata
OBJS = $(SRCS:.c=.o) $(MIGSTUBS)
MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
device-MIGSFLAGS="-DMACH_PAYLOAD_TO_PORT=ports_payload_get_name"

include ../Makeconf
