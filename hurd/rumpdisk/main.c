#include "../libmachdevrump/machdevrump.h"
#include <cthreads.h>
#include <mach.h>

int main()
{
  mach_print("register_block()\n");
  register_block ();

  mach_print("rump_device_init()\n");
  rump_device_init ();

  mach_print("trivfs_init()\n");
  trivfs_init ();

  mach_print("fork ds_server()\n");
  cthread_detach (cthread_fork (ds_server, NULL));

  mach_print("trivfs_server()\n");
  trivfs_server ();

  mach_print("exit!\n");
  return 0;
}
