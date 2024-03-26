/*
 * \brief   Hardware-interrupt subsystem
 * \author  Thomas Friebel <tf13@os.inf.tu-dresden.de>
 * \author  Christian Helmuth <ch12@os.inf.tu-dresden.de>
 * \date    2007-01-22
 *
 * FIXME could intloop_param freed after startup?
 */

#include <stdio.h>
#include <error.h>
#include <mach.h>
#include <hurd.h>

#include "libirqhelp/irqhelp.h"

#include "ddekit/semaphore.h"
#include "ddekit/memory.h"
#include "ddekit/thread.h"
#include "ddekit/interrupt.h"
#include "ddekit/printf.h"

#define MAX_INTERRUPTS   32

static struct
{
	int              handle_irq; /* nested irq disable count */
	ddekit_lock_t    irqlock;
	ddekit_thread_t  *irq_thread;
	ddekit_sem_t     *started;
	void             *irqhelp;   /* irqhelp instance for detaching from IRQ */
} ddekit_irq_ctrl[MAX_INTERRUPTS];

/*
 * Internal type for interrupt loop parameters
 */
struct intloop_params
{
	unsigned	irq;		/* irq number */
	void		*priv;		/* private token */
	void		(*init)(void *);/* thread_init */
};

void wrapped_server_loop(void *arg)
{
	struct intloop_params *params = (struct intloop_params *)arg;
	struct irq *irqhelp = ddekit_irq_ctrl[params->irq].irqhelp;

	params->init(params->priv);
	ddekit_sem_up(ddekit_irq_ctrl[params->irq].started);

	irqhelp_server_loop(irqhelp);
}

/**
 * Attach to hardware interrupt
 *
 * \param irq          IRQ number to attach to
 * \param shared       set to 1 if interrupt sharing is supported; set to 0
 *                     otherwise
 * \param thread_init  called just after DDEKit internal init and before any
 *                     other function
 * \param handler      IRQ handler for interrupt irq
 * \param priv         private token (argument for thread_init and handler)
 *
 * \return pointer to interrupt thread created
 */
ddekit_thread_t *ddekit_interrupt_attach(int irq, int shared,
                                         void(*thread_init)(void *),
                                         void(*handler)(void *), void *priv)
{
	struct intloop_params *params;
	ddekit_thread_t *thread;
	char thread_name[10];

	/* We cannot attach the interrupt to the irq which has been used. */
	if (ddekit_irq_ctrl[irq].irq_thread)
		return NULL;

	/* initialize info structure for interrupt loop */
	params = ddekit_simple_malloc(sizeof(*params));
	if (!params) return NULL;

	params->irq = irq;
	params->priv = priv;
	params->init = thread_init;

	ddekit_irq_ctrl[irq].started = ddekit_sem_init(0);
	ddekit_irq_ctrl[irq].handle_irq = 1; /* IRQ initial nesting is 1 */
	ddekit_lock_init_unlocked (&ddekit_irq_ctrl[irq].irqlock);

	/* construct name */
	snprintf(thread_name, sizeof(thread_name), "irq%02d", irq);

	ddekit_irq_ctrl[irq].irqhelp = irqhelp_install_interrupt_handler(irq, -1, -1, -1, handler, priv);

	/* create interrupt loop thread */
	thread = ddekit_thread_create(wrapped_server_loop, params, thread_name);
	if (!thread) {
		irqhelp_remove_interrupt_handler(ddekit_irq_ctrl[irq].irqhelp);
		ddekit_simple_free(params);
		return NULL;
	}
	ddekit_irq_ctrl[irq].irq_thread = thread;

	/* wait for intloop initialization result */
	ddekit_sem_down(ddekit_irq_ctrl[irq].started);
	ddekit_sem_deinit(ddekit_irq_ctrl[irq].started);

	return thread;
}

/**
 * Detach from interrupt by disabling it and then shutting down the IRQ
 * thread.
 */
void ddekit_interrupt_detach(int irq)
{
	ddekit_interrupt_disable(irq);

	ddekit_lock_lock (&ddekit_irq_ctrl[irq].irqlock);
	if (ddekit_irq_ctrl[irq].handle_irq == 0) {
		irqhelp_remove_interrupt_handler(ddekit_irq_ctrl[irq].irqhelp);
		ddekit_irq_ctrl[irq].irq_thread = NULL;
	}
	ddekit_lock_unlock (&ddekit_irq_ctrl[irq].irqlock);
}


void ddekit_interrupt_disable(int irq)
{
	if (ddekit_irq_ctrl[irq].irqlock) {
		ddekit_lock_lock (&ddekit_irq_ctrl[irq].irqlock);
		--ddekit_irq_ctrl[irq].handle_irq;
		ddekit_lock_unlock (&ddekit_irq_ctrl[irq].irqlock);
	}
}


void ddekit_interrupt_enable(int irq)
{
	if (ddekit_irq_ctrl[irq].irqlock) {
		ddekit_lock_lock (&ddekit_irq_ctrl[irq].irqlock);
		++ddekit_irq_ctrl[irq].handle_irq;
		if (ddekit_irq_ctrl[irq].handle_irq > 0)
			irqhelp_enable_irq(ddekit_irq_ctrl[irq].irqhelp);
		ddekit_lock_unlock (&ddekit_irq_ctrl[irq].irqlock);
	}
}

void interrupt_init (void)
{
	irqhelp_init();
}
