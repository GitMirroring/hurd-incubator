#include "machdev.h"
#include <hurd/machdev.h>
#include <hurd/machdevdde.h>

void machdev_init(void)
{
	machdevdde_register_net();
	machdev_device_init();
	machdev_trivfs_init();
}

void machdev_run1 (void *arg)
{
	machdevdde_server(arg);
}

void machdev_run2(void)
{
	machdev_trivfs_server();
}
