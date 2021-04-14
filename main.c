#include <dde26.h> /* l4dde26_*() */
#include <dde26_net.h> /* l4dde26 networking */

#include <pthread.h>

#include <linux/netdevice.h> /* struct sk_buff */
#include <linux/pci.h> /* pci_unregister_driver() */
#include <linux/init.h>  // initcall()
#include <linux/delay.h> // msleep()

#include "machdev.h"
#include "check_kernel.h"

int using_std = 1;

int main(int argc, char **argv)
{
	check_kernel();

	l4dde26_init();
	l4dde26_process_init();
	l4dde26_softirq_init();

	printk("Initializing skb subsystem\n");
	skb_init();

	l4dde26_do_initcalls();

	machdev_init(argc, argv);

	ddekit_thread_create (machdev_run1, NULL, "ds_server");
	machdev_run2();

	/* Let the other threads do their job */
	pthread_exit(NULL);

	return 0;
}
