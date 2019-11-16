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

/* One of these is associated with each open instance of a device.  */
struct block_data
{
  struct port_info port;	/* device port */
  struct emul_device device;	/* generic device structure */
  dev_mode_t mode;
  int rump_fd;                  /* block device fd handle */
  uint64_t media_size;          /* total block device size */
  uint32_t block_size;          /* size in bytes of 1 sector */
  bool taken;			/* simple refcount */
};

/* Return a send right associated with network device ND.  */
static mach_port_t
dev_to_port (void *nd)
{
  return (nd
	  ? ports_get_send_right (nd)
	  : MACH_PORT_NULL);
}

static struct block_data block_ref;
static struct device_emulation_ops rump_block_emulation_ops;

#define DISK_NAME_LEN 32

/* BSD name of whole disk device is /dev/wdXd 
 * but we will receive /dev/wdX as the name */
static char *
translate_name (char *name)
{
  char *ret;
  ret = malloc (DISK_NAME_LEN);
  sprintf (ret, "%sd", name);
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

static void
device_init (void)
{
  rump_init();
}

static io_return_t
device_close (void *d)
{
  io_return_t err;
  struct block_data *bd = d;
  
  err = rump_errno2host (rump_sys_close (bd->rump_fd));
  block_ref.taken = false;
  
  return err;
}

static void
device_dealloc (void *d)
{
  rump_sys_reboot(0, NULL);
}

static io_return_t
device_open (mach_port_t reply_port, mach_msg_type_name_t reply_port_type,
	     dev_mode_t mode, char *name, device_t *devp,
	     mach_msg_type_name_t *devicePoly)
{
  io_return_t err = D_SUCCESS;
  struct block_data *bd = &block_ref;
  char *dev_name;

  mach_print("device open\n");
  dev_name = translate_name (name);

  err = create_device_port (sizeof (*bd), &bd);

  if (block_ref.taken)
  {
    bd->rump_fd = block_ref.rump_fd;
    bd->mode = block_ref.mode;
    bd->media_size = block_ref.media_size;
    bd->block_size = block_ref.block_size;
  }
  else
  {
    bd->rump_fd = rump_sys_open (dev_name, dev_mode_to_rump_mode (mode));
    if (bd->rump_fd < 0)
    {
      mach_print ("rump_sys_open fails: ");
      mach_print (dev_name);
      mach_print ("\n");
      err = rump_errno2host (errno);
      goto out;
    }
    block_ref.taken = true;
    block_ref.rump_fd = bd->rump_fd;

    uint64_t media_size;
    err = rump_sys_ioctl (bd->rump_fd, DIOCGMEDIASIZE, &media_size);
    if (err < 0)
    {
      mach_print ("DIOCGMEDIASIZE ioctl fails\n");
      err = D_NO_SUCH_DEVICE;
      goto out;
    }
 
    uint32_t block_size;
    err = rump_sys_ioctl (bd->rump_fd, DIOCGSECTORSIZE, &block_size);
    if (err < 0)
    {
      mach_print ("DIOCGSECTORSIZE ioctl fails\n");
      err = D_NO_SUCH_DEVICE;
      goto out;
    }
    block_ref.media_size = bd->media_size = media_size;
    block_ref.block_size = bd->block_size = block_size;
    block_ref.mode = bd->mode = mode;
  }

  bd->device.emul_data = bd;
  bd->device.emul_ops = &rump_block_emulation_ops;
  err = D_SUCCESS;

out:
  free (dev_name);
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
    err = MIG_NO_REPLY;
    return err;
  }

  err = rump_sys_write (bd->rump_fd, data, count);
  if (err < 0)
  {
    err = MIG_NO_REPLY;
    return err;
  }
  else
  {
    *bytes_written = err;
    err = D_SUCCESS;
    ds_device_write_reply (reply_port, reply_port_type, err, *bytes_written);
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
  char *buf;
  int pagesize = sysconf(_SC_PAGE_SIZE);
  int npages = (count + pagesize - 1) / pagesize;

  if ((bd->mode & D_READ) == 0)
    return D_INVALID_OPERATION;

  if (count == 0)
    return D_SUCCESS;

  *data = 0;
  buf = mmap (NULL, npages * pagesize, PROT_READ|PROT_WRITE,
              MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
  if (buf == MAP_FAILED)
    return errno;
  
  err = rump_sys_lseek(bd->rump_fd, bn * bd->block_size, SEEK_SET);
  if (err < 0)
  {
    err = MIG_NO_REPLY;
    return err;
  }

  err = rump_sys_read(bd->rump_fd, buf, count);
  if (err < 0)
  {
    err = MIG_NO_REPLY;
    return err;
  }
  else
  {
    *bytes_read = err;
    err = D_SUCCESS;
    ds_device_read_reply (reply_port, reply_port_type, err, buf, *bytes_read);
    return err;
  }
}

static io_return_t
device_get_status (void *d, dev_flavor_t flavor, dev_status_t status,
		   mach_msg_type_number_t *count)
{
  struct block_data *bd = d;
  mach_print("device_get_status\n");

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
  device_init,
  NULL,
  device_dealloc,
  dev_to_port,
  device_open,
  device_close,
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
