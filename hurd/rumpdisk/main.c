#include "../libmachdevrump/machdevrump.h"
#include <cthreads.h>
#include <mach.h>

int main()
{
  register_block ();
  rump_device_init ();
  trivfs_init ();
  cthread_detach (cthread_fork (ds_server, NULL));
  trivfs_server ();
  return 0;
}
