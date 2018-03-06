#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <error.h>
#include <mach.h>
#include <hurd.h>
#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>

#include "ddekit/memory.h"
#include "ddekit/semaphore.h"
#include "ddekit/condvar.h"
#include "ddekit/thread.h"

#define DDEKIT_THREAD_STACK_SIZE 0x2000 /* 8 KB */

//static struct ddekit_slab *ddekit_stack_slab = NULL;

struct _ddekit_private_data {
	ddekit_condvar_t *sleep_cond;
	/* point to the thread who has the private data. */
	struct ddekit_thread *thread;
};

struct ddekit_thread {
	pthread_t thread;
	char *name;
	struct _ddekit_private_data *private;
	void *user;
};

struct ddekit_sem
{
	sem_t sem;
};

static __thread struct ddekit_thread *thread_self;

static void _thread_cleanup ()
{
	ddekit_condvar_deinit (thread_self->private->sleep_cond);
	ddekit_simple_free (thread_self->private);
	ddekit_simple_free (thread_self->name);
	ddekit_simple_free (thread_self);
}

static void setup_thread (struct ddekit_thread *t, const char *name) {
	struct _ddekit_private_data *private_data;

	if (name) {
		char *cpy = NULL;

		cpy = ddekit_simple_malloc (strlen (name) + 1);
		if (cpy == NULL)
			error (0, 0, "fail to allocate memory");
		else 
			strcpy (cpy, name);

		t->name = cpy;
	}

	private_data = (struct _ddekit_private_data *)
	  ddekit_simple_malloc (sizeof (*private_data));

	private_data->sleep_cond = ddekit_condvar_init ();
	private_data->thread = t;

	t->private = private_data;
}

ddekit_thread_t *ddekit_thread_setup_myself(const char *name) {
	ddekit_thread_t *td = (ddekit_thread_t *) malloc (sizeof (*td));
	setup_thread (td, name);
	thread_self = td;
	return td;
}

typedef struct
{
  void (*fun)(void *);
  void *arg;
  struct ddekit_thread *td;
  pthread_cond_t cond;
  pthread_mutex_t lock;
  int status;
} priv_arg_t;

static void* _priv_fun (void *arg)
{
  priv_arg_t *priv_arg = arg;
  thread_self = priv_arg->td;
  /* We wait until the initialization of the thread is finished. */
  pthread_mutex_lock (&priv_arg->lock);
  while (!priv_arg->status)
    pthread_cond_wait (&priv_arg->cond, &priv_arg->lock);
  pthread_mutex_unlock (&priv_arg->lock);

  priv_arg->fun(priv_arg->arg);
  free (priv_arg->arg);
  _thread_cleanup ();
  return NULL;
}

ddekit_thread_t *ddekit_thread_create(void (*fun)(void *), void *arg, const char *name) {
	ddekit_thread_t *td = (ddekit_thread_t *) malloc (sizeof (*td));
	setup_thread (td, name);

	priv_arg_t *priv_arg = (priv_arg_t *) malloc (sizeof (*priv_arg));
	priv_arg->fun = fun;
	priv_arg->arg = arg;
	priv_arg->td = td;
	pthread_cond_init (&priv_arg->cond, NULL);
	pthread_mutex_init (&priv_arg->lock, NULL);
	priv_arg->status = 0;

	pthread_create (&td->thread, NULL, _priv_fun, priv_arg);
	pthread_detach (td->thread);

	/* Tell the new thread that initialization has been finished. */
	pthread_mutex_lock (&priv_arg->lock);
	priv_arg->status = 1;
	pthread_cond_signal (&priv_arg->cond);
	pthread_mutex_unlock (&priv_arg->lock);

	return td;
}

ddekit_thread_t *ddekit_thread_myself(void) {
	return thread_self;
}

void ddekit_thread_set_data(ddekit_thread_t *thread, void *data) {
	thread->user = data;
}

void ddekit_thread_set_my_data(void *data) {
	ddekit_thread_set_data(ddekit_thread_myself(), data);
}

void *ddekit_thread_get_data(ddekit_thread_t *thread) {
	return thread->user;
}

void *ddekit_thread_get_my_data() {
	return ddekit_thread_get_data(ddekit_thread_myself());
}

void ddekit_thread_msleep(unsigned long msecs) {
	int ret;
	struct timespec rgt;

	rgt.tv_sec = (time_t) (msecs / 1000);
	rgt.tv_nsec = (msecs % 1000) * 1000 * 1000;
	ret = nanosleep (&rgt , NULL);
	if (ret < 0)
		error (0, errno, "nanosleep");
}

void ddekit_thread_usleep(unsigned long usecs) {
	int ret;
	struct timespec rgt;

	rgt.tv_sec = (time_t) (usecs / 1000 / 1000);
	rgt.tv_nsec = (usecs % (1000 * 1000)) * 1000;
	ret = nanosleep (&rgt , NULL);
	if (ret < 0)
		error (0, errno, "nanosleep");
}


void ddekit_thread_nsleep(unsigned long nsecs) {
	int ret;
	struct timespec rgt;

	rgt.tv_sec = (time_t) (nsecs / 1000 / 1000 / 1000);
	rgt.tv_nsec = nsecs % (1000 * 1000 * 1000);
	ret = nanosleep (&rgt , NULL);
	if (ret < 0)
		error (0, errno, "nanosleep");
}

void ddekit_thread_sleep(ddekit_lock_t *lock) {
	// TODO pthread_cond_wait cannot guarantee that the thread is 
	// woke up by another thread, maybe by signals.
	// Does it matter here?
	// If it does, use pthread_hurd_cond_wait_np.
	ddekit_condvar_wait (thread_self->private->sleep_cond, lock);
}

void  ddekit_thread_wakeup(ddekit_thread_t *td) {
	if (td->private == NULL)
		return;
	ddekit_condvar_signal (td->private->sleep_cond);
}

void  ddekit_thread_exit() {
	_thread_cleanup ();
	pthread_exit (NULL);
}

const char *ddekit_thread_get_name(ddekit_thread_t *thread) {
	return thread->name;
}

void ddekit_thread_schedule(void)
{
	swtch_pri (0);
}

void ddekit_yield(void)
{
	swtch_pri (0);
}

void ddekit_init_threads() {
	ddekit_thread_setup_myself ("main");
}

/**********************************************************************
 *			    semaphore
 **********************************************************************/

static int _sem_timedwait_internal (ddekit_sem_t *restrict sem, int timeout)
{
	int ret;
	if (timeout)
	{
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		ts.tv_nsec += (timeout%1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_nsec -= 1000000000;
			ts.tv_sec += 1;
		}
		ts.tv_sec += timeout/1000;
		do
			ret = sem_timedwait(&sem->sem, &ts);
		while (ret == -1 && errno == EINTR);
		return ret;
	}
	else
	{
		do
			ret = sem_wait(&sem->sem);
		while (ret == -1 && errno == EINTR);
		return 0;
	}
}

ddekit_sem_t *ddekit_sem_init(int value) {
	ddekit_sem_t *sem =
	  (ddekit_sem_t *) ddekit_simple_malloc (sizeof (*sem));

	sem_init(&sem->sem, 0, value);
	return sem;
}

void ddekit_sem_deinit(ddekit_sem_t *sem) {
	sem_destroy(&sem->sem);
	ddekit_simple_free(sem);
}

void ddekit_sem_down(ddekit_sem_t *sem) {
	_sem_timedwait_internal (sem, 0);
}

/* returns 0 on success, != 0 when it would block */
int  ddekit_sem_down_try(ddekit_sem_t *sem) {
	return sem_trywait(&sem->sem);
}

/* returns 0 on success, != 0 on timeout */
int  ddekit_sem_down_timed(ddekit_sem_t *sem, int timo) {
	/* wait for up to timo milliseconds */
	return _sem_timedwait_internal (sem, timo);
}

void ddekit_sem_up(ddekit_sem_t *sem) {
	sem_post(&sem->sem);
}

