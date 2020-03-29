/*
   Copyright (C) 2010 Free Software Foundation, Inc.
   Written by Zheng Da.

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

#include <mach.h>
#include <device/device.h>

#include <ddekit/printf.h>

#include "linux-errno.h"

int
linux_to_mach_error (int err)
{
  switch (err)
    {
    case 0:
      return D_SUCCESS;

    case -EPERM:
      return D_INVALID_OPERATION;

    case -EIO:
      return D_IO_ERROR;

    case -ENXIO:
      return D_NO_SUCH_DEVICE;

    case -EACCES:
      return D_INVALID_OPERATION;

    case -EFAULT:
      return D_INVALID_SIZE;

    case -EBUSY:
      return D_ALREADY_OPEN;

    case -EINVAL:
      return D_INVALID_SIZE;

    case -EROFS:
      return D_READ_ONLY;

    case -EWOULDBLOCK:
      return D_WOULD_BLOCK;

    case -ENOMEM:
      return D_NO_MEMORY;

    default:
      ddekit_printf ("linux_to_mach_error: unknown code %d\n", err);
      return D_IO_ERROR;
    }
}
