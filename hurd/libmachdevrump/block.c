/*
 * Rump block driver support
 *
 * Copyright (C) 2019 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>

#include "mach_U.h"

#include <mach.h>
#include <hurd.h>

#define MACH_INCLUDE

#include "ds_routines.h"
#include "vm_param.h"
#include "device_reply_U.h"
#include "dev_hdr.h"
#include "mach_glue.h"

#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumperrno2host.h>

/* rump ioctl stuff */
#define IOCPARM_MASK    0x1fff
#define IOCPARM_SHIFT   16
#define IOCGROUP_SHIFT  8
#define _IOC(inout, group, num, len) \
    ((inout) | (((len) & IOCPARM_MASK) << IOCPARM_SHIFT) | \
    ((group) << IOCGROUP_SHIFT) | (num))
#define IOC_OUT         (unsigned long)0x40000000
#define _IOR(g,n,t)     _IOC(IOC_OUT,   (g), (n), sizeof(t))
#define  DIOCGMEDIASIZE  _IOR('d', 132, off_t)
#define  DIOCGSECTORSIZE _IOR('d', 133, unsigned int)

static bool rump_init_done = false;

/* One of these is associated with each open instance of a device.  */
struct block_data
{
  struct port_info port;	/* device port */
  struct emul_device device;	/* generic device structure */
  dev_mode_t mode;
  int rump_fd;                  /* block device fd handle */
  off_t media_size;             /* total block device size */
  uint32_t block_size;          /* size in bytes of 1 sector */
};

/* Return a send right associated with network device ND.  */
static mach_port_t
dev_to_port (void *nd)
{
  return (nd
	  ? ports_get_send_right (nd)
	  : MACH_PORT_NULL);
}

static struct device_emulation_ops rump_block_emulation_ops;

#define DISK_NAME_LEN 32

// FIXME Need a better function
/* Parse the device NAME.
   Set *SLICE to be the DOS partition and
   *PART the BSD/Mach partition, if any.  */
static char *
translate_name (char *name, int *slice, int *part)
{
  char *p, *q;
  char *ret;
  int disk_num;

  /* Parse name into name, unit, DOS partition (slice) and partition.  */
  for (*slice = 0, *part = -1, p = name; isalpha (*p); p++)
    ;
  if (p == name || ! isdigit (*p))
    return NULL;
  disk_num = strtol (p, &p, 0);
  if (disk_num < 0 || disk_num > 26)
    return NULL;
//  do
//    p++;
//  while (isdigit (*p));
  if (*p)
    {
      q = p;
      if (*q == 's' && isdigit (*(q + 1)))
	{
	  q++;
	  do
	    *slice = *slice * 10 + *q++ - '0';
	  while (isdigit (*q));
	  if (! *q)
	    goto find_major;
	}
      if (! isalpha (*q) || *(q + 1))
	return NULL;
      *part = *q - 'a';
    }

find_major:
  ret = malloc (DISK_NAME_LEN);
  //sprintf (ret, "hd%c", 'a' + disk_num);
  sprintf (ret, "/dev/wd%cd", '0' + disk_num);
  return ret;
}

static int dev_mode_to_rump_mode(const dev_mode_t mode)
{
  int ret = 0;
  if (mode & D_READ)
  {
    if (mode & D_WRITE)
      ret = RUMP_O_RDWR;
    else
      ret = RUMP_O_RDONLY;
  }
  else
  {
    if (mode & D_WRITE)
      ret = RUMP_O_WRONLY;
  }
  return ret;
}

static io_return_t
device_open (mach_port_t reply_port, mach_msg_type_name_t reply_port_type,
	     dev_mode_t mode, char *name, device_t *devp,
	     mach_msg_type_name_t *devicePoly)
{
  io_return_t err = D_SUCCESS;
  struct block_data *bd = NULL;
  int slice, part;
  // FIXME Hardcoded for now let's get the disk working!
  const char *dev_name = "/dev/wd0d";

  // TODO Need to check whether the device has been opened before.
  // if it has been opened with the same `flag', return the same port,
  // otherwise, return a different port.
  // Need to have a reference to count the times opened.
  
  mach_print("hello open\n");
  // FIXME dev_name = translate_name (name, &slice, &part);
  if (dev_name == NULL)
  {
    mach_print ("no such device\n");
    return D_NO_SUCH_DEVICE;
  }

  err = create_device_port (sizeof (*bd), &bd);
  if (err)
    {
      mach_print ("after create_device_port: cannot create a port\n");
      goto out;
    }

  if (!rump_init_done)
  {
    if (rump_init() != 0)
    {
      err = EPERM;
      mach_print("EPERM on rump_init()\n");
      goto out;
    }
    else
      rump_init_done = true;
  }

  bd->rump_fd = rump_sys_open (dev_name, dev_mode_to_rump_mode (mode));
  if (bd->rump_fd < 0)
  {
    mach_print ("rump_sys_open fails:\n");
    mach_print (dev_name);
    mach_print ("\n");
    err = rump_errno2host (errno);
    goto out;
  }

  off_t media_size;
  err = rump_sys_ioctl (bd->rump_fd, DIOCGMEDIASIZE, &media_size);
  if (err != 0)
  {
    mach_print ("DIOCGMEDIASIZE ioctl fails\n");
    rump_sys_close(bd->rump_fd);
    return rump_errno2host (errno);
  }

  uint32_t block_size;
  err = rump_sys_ioctl (bd->rump_fd, DIOCGSECTORSIZE, &block_size);
  if (err != 0)
  {
    mach_print ("DIOCGSECTORSIZE ioctl fails\n");
    rump_sys_close(bd->rump_fd);
    return rump_errno2host (errno);
  }

  bd->device.emul_data = bd;
  bd->device.emul_ops = &rump_block_emulation_ops;
  bd->mode = mode;
  bd->media_size = media_size;
  bd->block_size = block_size;

  mach_print ("media_size:\n");
  mach_print (media_size);
  mach_print ("block_size\n");
  mach_print (block_size);
out:
  //free (dev_name);
  if (err)
    {
      if (bd)
	{
	  ports_destroy_right (bd);
	  bd = NULL;
	}
    }
  else
    {
      *devp = ports_get_send_right (bd);
      ports_port_deref (bd);
      *devicePoly = MACH_MSG_TYPE_MOVE_SEND;
    }
  return err;
}

static io_return_t
device_write (void *d, mach_port_t reply_port,
	      mach_msg_type_name_t reply_port_type, dev_mode_t mode,
	      recnum_t bn, io_buf_ptr_t data, unsigned int count,
	      int *bytes_written)
{
  struct block_data *bd = d;
  io_return_t err = D_SUCCESS;

  if ((bd->mode & D_WRITE) == 0)
    return D_INVALID_OPERATION;

  err = rump_sys_lseek (bd->rump_fd, bn * bd->block_size, SEEK_SET);
  if (err < 0)
  {
    err = rump_errno2host(errno);
    ds_device_write_reply (reply_port, reply_port_type, err, 0);
    return err;
  }

  err = rump_sys_write (bd->rump_fd, data, count);
  if (err > 0)
  {
    *bytes_written = err;
    return MIG_NO_REPLY;
  }
  else
  {
    err = rump_errno2host(errno);
    ds_device_write_reply (reply_port, reply_port_type, err, 0);
    return err;
  }
}

static io_return_t
device_read (void *d, mach_port_t reply_port,
	     mach_msg_type_name_t reply_port_type, dev_mode_t mode,
	     recnum_t bn, int count, io_buf_ptr_t *data,
	     unsigned *bytes_read)
{
  struct block_data *bd = d;
  io_return_t err = D_SUCCESS;

  if ((bd->mode & D_READ) == 0)
    return D_INVALID_OPERATION;

  if (count == 0)
    return 0;

  *data = 0;

  err = rump_sys_lseek(bd->rump_fd, bn * bd->block_size, SEEK_SET);
  if (err < 0)
  {
    err = rump_errno2host(errno);
    ds_device_read_reply (reply_port, reply_port_type, err, *data, 0);
    return err;
  }

  err = rump_sys_read(bd->rump_fd, data, count);
  if (err > 0)
  {
    *bytes_read = err;
    err = 0;
    return MIG_NO_REPLY;
  }
  else
  {
    err = rump_errno2host(errno);
    ds_device_read_reply (reply_port, reply_port_type, err, *data, 0);
    return err;
  }
}

static io_return_t
device_get_status (void *d, dev_flavor_t flavor, dev_status_t status,
		   mach_msg_type_number_t *count)
{
  struct block_data *bd = d;
  mach_print("device_get_status");

  switch (flavor)
  {
  case DEV_GET_SIZE:
    status[DEV_GET_SIZE_RECORD_SIZE] = bd->block_size;
    status[DEV_GET_SIZE_DEVICE_SIZE] = bd->media_size;
    *count = 2;
    break;
  case DEV_GET_RECORDS:
    status[DEV_GET_RECORDS_RECORD_SIZE] = bd->block_size;
    status[DEV_GET_RECORDS_DEVICE_RECORDS] = bd->media_size / bd->block_size;
    *count = 2;
    break;
  default:
    return D_INVALID_OPERATION;
    break;
  }
  return D_SUCCESS;
}

static struct device_emulation_ops rump_block_emulation_ops =
{
  NULL,
  NULL,
  NULL,
  dev_to_port,
  device_open,
  NULL,
  device_write,
  NULL,
  device_read,
  NULL,
  NULL,
  device_get_status,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

void register_block()
{
	extern void reg_dev_emul (struct device_emulation_ops *ops);
	reg_dev_emul (&rump_block_emulation_ops);
}
