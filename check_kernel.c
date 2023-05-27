/*!
   Copyright (C) 2012 Free Software Foundation, Inc.
   Written by Samuel Thibault.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h>

#include "check_kernel.h"

void check_kernel(void) {
	mach_port_t device_master;

	if (! get_privileged_ports (0, &device_master))
	    {
		device_t device;

		if (! device_open (device_master, D_READ, "eth0", &device))
		    {
			error_t err;

			fprintf (stderr, "Kernel is already driving a network device, starting devnode instead of netdde\n");
			err = execl ("/hurd/devnode", "devnode", "-n", "eth0", "eth0", NULL);
			error (1, err, "Invocation of devnode failed");
		    }
	    }

	mach_port_deallocate (mach_task_self (), device_master);
}
