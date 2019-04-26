#ifndef __MACH_GLUE_H__
#define __MACH_GLUE_H__

/* block device */
struct block_device;
struct block_device *open_block_dev (char *name, int part, dev_mode_t mode);
int block_dev_rw (struct block_device *dev, int sectornr,
		  char *data, int count, int rw, void (*write_done) (int err));

#endif
