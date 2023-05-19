#include "local.h"

void *crash_notes = NULL;

void touch_nmi_watchdog(void)
{
	WARN_UNIMPL;
}

unsigned long pci_mem_start = 0xABCDABCD;
