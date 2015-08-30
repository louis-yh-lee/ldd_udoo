/*
 * jiq.c -- the just-in-queue module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: jiq.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>     /* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>  /* error codes */
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> /* tasklets */
#include <linux/seq_file.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = 1;
module_param(delay, long, 0);


/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */

#define LIMIT	(PAGE_SIZE-128)	/* don't print any more after this size */

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reched, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD (jiq_wait);

/*
 * Keep track of info we need between task queue runs.
 */
typedef struct clientdata {
	struct seq_file *s;
	unsigned long jiffies;
	long delay;
	int wakeup;
	union w {
		struct work_struct jiq_work;
		struct delayed_work jiq_work_delayed;
	} w;
} clientdata_t;

static struct clientdata jiq_data;
static struct clientdata jiq_data_delayed; 

#define SCHEDULER_QUEUE ((task_queue *) 1)

#define DECLARE_SEQ_FILE_OPEN(_n) \
	static int _n##_open(struct inode *i, struct file *f)	\
	{return single_open(f, _n##_show , NULL);}

#define DECLARE_SEQ_FILE_FO(_n) \
	static const struct file_operations _n##_fo = {		\
		.open = _n##_open,				\
		.read = seq_read,				\
		.llseek = seq_lseek,				\
		.release = single_release};

static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);


/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(void *ptr)
{
	struct clientdata *d = ptr;
	unsigned long j = jiffies;
	struct seq_file *s = d->s;
	

	if (s->count > LIMIT) { 
		wake_up_interruptible(&jiq_wait);
		return 0;
	}

	if (s->count == 0)
		seq_printf(s,"    time  delta preempt   pid cpu command\n");

  	/* intr_count is only exported since 1.3.5, but 1.99.4 is needed anyways */
	seq_printf(s, "%9li  %4li     %3i %5i %3i %s\n",
			j, j - d->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);

	d->jiffies = j;
	return 1;
}


/*
 * Call jiq_print from a work queue
 * delayed work queue synchronization
 * should be a form of  work_func_t
 */
static void jiq_print_wq(struct work_struct *w)
{
	struct clientdata *data = container_of(w, struct clientdata, w.jiq_work);
    
	if (! jiq_print (data))
		return;
    
	schedule_work(&data->w.jiq_work);
}

/* in the case of delayed_work */
static void jiq_print_wqd(struct work_struct *w)
{
	struct clientdata *data = container_of(w, struct clientdata, w.jiq_work_delayed.work);
    
	if (! jiq_print (data))
		return;
    
	schedule_delayed_work(&data->w.jiq_work_delayed, data->delay);
}

static int jiq_read_wq_show(struct seq_file *s, void *data)
{
	struct clientdata *d = (clientdata_t *)data;
	DEFINE_WAIT(wait);
	
	d->s = s;
	d->jiffies = jiffies;      /* initial time */
	d->delay = 0;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_work(&d->w.jiq_work);
	schedule();
	finish_wait(&jiq_wait, &wait);

	return 0;
}
DECLARE_SEQ_FILE_OPEN(jiq_read_wq)
DECLARE_SEQ_FILE_FO(jiq_read_wq)

static int jiq_read_wq_delayed_show(struct seq_file *s, void *data)
{
	struct clientdata *d = (clientdata_t *)data;
	DEFINE_WAIT(wait);

	d->s = s;
	d->jiffies = jiffies;      /* initial time */
	d->delay = delay;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&d->w.jiq_work_delayed, delay);
	schedule();
	finish_wait(&jiq_wait, &wait);

	return 0;
}
DECLARE_SEQ_FILE_OPEN(jiq_read_wq_delayed)
DECLARE_SEQ_FILE_FO(jiq_read_wq_delayed)



/*
 * Call jiq_print from a tasklet
 */
static void jiq_print_tasklet(unsigned long ptr)
{
	if (jiq_print ((void *) ptr))
		tasklet_schedule (&jiq_tasklet);
}

static int jiq_read_tasklet_show(struct seq_file *s, void *data)
{
	struct clientdata *d = (clientdata_t *)data;

	d->s = s;
	d->jiffies = jiffies;      /* initial time */
	d->wakeup = 0;

	tasklet_schedule(&jiq_tasklet);
	/* when wakeup is 1, then waek-up */
	wait_event_interruptible(jiq_wait, d->wakeup);    /* sleep till completion */

	return 0;
}
DECLARE_SEQ_FILE_OPEN(jiq_read_tasklet)
DECLARE_SEQ_FILE_FO(jiq_read_tasklet)



/*
 * This one, instead, tests out the timers.
 */

static struct timer_list jiq_timer;

static void jiq_timedout(unsigned long ptr)
{
	struct clientdata *d = (void *)ptr;
	jiq_print((void *)d);            /* print a line */
	d->wakeup = 1;
	wake_up_interruptible(&jiq_wait);  /* awake the process */
}

static int jiq_read_run_timer_show(struct seq_file *s, void *data)
{
	struct clientdata *d = (clientdata_t *)data;
	d->s = s;
	d->jiffies = jiffies;
	d->wakeup = 0;

	init_timer(&jiq_timer);              /* init the timer structure */
	jiq_timer.function = jiq_timedout;
	jiq_timer.data = (unsigned long)d;
	jiq_timer.expires = jiffies + HZ; /* one second */

	jiq_print((void *)d);   /* print and go to sleep */
	add_timer(&jiq_timer);
	wait_event_interruptible(jiq_wait, d->wakeup);  /* RACE */
	del_timer_sync(&jiq_timer);  /* in case a signal woke us up */
    
	return 0;
}
DECLARE_SEQ_FILE_OPEN(jiq_read_run_timer)
DECLARE_SEQ_FILE_FO(jiq_read_run_timer)


/*
 * the init/clean material
 */

static int jiq_init(void)
{

	/* this line is in jiq_init() */
	INIT_WORK(&jiq_data.w.jiq_work, jiq_print_wq);
	INIT_DELAYED_WORK(&jiq_data_delayed.w.jiq_work_delayed, jiq_print_wqd);

	proc_create_data("jiqwq", 0, NULL, &jiq_read_wq_fo, &jiq_data);
	proc_create_data("jiqwqdelay", 0, NULL, &jiq_read_wq_delayed_fo, &jiq_data_delayed);
	proc_create_data("jitimer", 0, NULL, &jiq_read_run_timer_fo, &jiq_data);
	proc_create_data("jiqtasklet", 0, NULL, &jiq_read_tasklet_fo, &jiq_data);

	return 0; /* succeed */
}

static void jiq_cleanup(void)
{
	remove_proc_entry("jiqwq", NULL);
	remove_proc_entry("jiqwqdelay", NULL);
	remove_proc_entry("jitimer", NULL);
	remove_proc_entry("jiqtasklet", NULL);
}


module_init(jiq_init);
module_exit(jiq_cleanup);
