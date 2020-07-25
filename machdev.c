#include "machdev.h"
#include <hurd/machdev.h>
#include <hurd/machdevdde.h>

static mach_port_t bootstrap = MACH_PORT_NULL;

void machdev_init(void)
{
	machdevdde_register_net();
	machdev_device_init();
	machdev_trivfs_init(MACH_PORT_NULL, NULL, &bootstrap);
}

void machdev_run1 (void *arg)
{
	machdevdde_server(arg);
}

void machdev_run2(void)
{
	machdev_trivfs_server(bootstrap);
}
