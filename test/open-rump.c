#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sys/io.h>
#include <rump/rump_syscalls.h>
#include <rump/rump.h>
#include <rump/rumperrno2host.h>

static void
die (const char *msg)
{
	int err = rump_errno2host(errno);
	fprintf(stderr, "%s : errno = 0x%08x\n", msg, err);
	exit(rump_errno2host(errno));
}

void mount(char *blockdev)
{
	int fd;
	int err;
	char buf[8192] = {0};
	if ((fd = rump_sys_open(blockdev, RUMP_O_RDONLY)) == -1)
                die("open disk failed\n");
	else {
		printf("found %s\n", blockdev);
		err = rump_sys_read(fd, buf, 512);
		for (int i = 0; i < 512; i++) {
			printf ("%0x ", buf[i]);
		}
		printf ("\nERR=%d\n", err);
	}
}

int main(int argc, char **argv)
{
	rump_init();
	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/xxx\n", argv[0]);
		exit(1);
	}
	mount(argv[1]);
	rump_sys_reboot(0, NULL);
	exit(0);
}
