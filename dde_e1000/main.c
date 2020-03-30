#include <dde26.h> /* l4dde26_*() */
#include <dde26_net.h> /* l4dde26 networking */

#include <linux/netdevice.h> /* struct sk_buff */
#include <linux/pci.h> /* pci_unregister_driver() */
#include <linux/init.h>  // initcall()
#include <linux/delay.h> // msleep()

#include <hurd/machdev.h>
#include <hurd/machdevdde.h>

int using_std = 1;

int main(int argc, char **argv)
{
	pthread_t thread;

	l4dde26_init();
	l4dde26_process_init();
	l4dde26_softirq_init();

	printk("Initializing skb subsystem\n");
	skb_init();

	l4dde26_do_initcalls();

	machdevdde_register_net();
	machdev_device_init();
	machdev_trivfs_init();

	pthread_create (&thread, NULL, machdevdde_server, NULL);
	pthread_detach (thread);
	machdev_trivfs_server();

	return 0;
}
