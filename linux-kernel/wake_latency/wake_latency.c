/**
 * A kernel module for measuring scheduling or wake-up latencies of
 * different kernel tasks:
 *   1) workqueue
 *   2) tasklet
 *   3) kernel thread
 *
 * The tests can be run through userspace app wl_test.c
 *
 * License: GPL
 * History:
 *   2010/07/24		Danke Xie		Initial version
 */

/* Define this to enable pr_debug messages */
// #define DEBUG

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>

#include "wake_latency.h"
#include "wake_latency_device.h"

static struct latency_device *device;

/**
 * The "work" task class
 */
static struct work_data_s {
	struct task_class *task_ptr;
	struct work_struct work;
	struct workqueue_struct *work_q;
} work_data;

static void work_func(struct work_struct *work)
{
	struct work_data_s *data = container_of(work, struct work_data_s, work);
	struct task_class *task = data->task_ptr;

	/* do the work */

	/* signal completion */
	task->complete(task->complete_data);
	atomic_set(&task->busy, 0);
}

int work_init(struct task_class *task, void (*complete)(unsigned long))
{
	struct work_data_s *data = task->task_data;

	spin_lock_init(&task->lock);
	INIT_WORK(&data->work, work_func);
	data->task_ptr = task;
	task->complete = complete;

	switch (task->subtype) {
	case WLS_WORKQUEUE_SELF:
		data->work_q = create_workqueue("wl_workq");
		if (!data->work_q)
			return -ENOMEM;
		break;
	default:
		break;
	}

	return 0;
}

/* context: interrupt */
int work_invoke(struct task_class *task, unsigned long complete_data)
{
	struct work_data_s *data = task->task_data;
	unsigned long flags;

	BUG_ON(data != &work_data);

	if (atomic_read(&task->busy))
		return -EBUSY;

	/* place the work in queue */
	spin_lock_irqsave(&task->lock, flags);
	atomic_set(&task->busy, 1);
	task->complete_data = complete_data;
	spin_unlock_irqrestore(&task->lock, flags);

	switch (task->subtype) {
	case WLS_WORKQUEUE_SELF:
		queue_work(data->work_q, &data->work);
		break;
	case WLS_WORKQUEUE_GLOBAL:
		/* falls thru */
	default:
		schedule_work(&data->work);
		break;
	}

	return 0;
}

/* context: may sleep */
int work_remove(struct task_class *task)
{
	struct work_data_s *data = task->task_data;

	while (atomic_read(&task->busy))
		schedule();

	switch (task->subtype) {
	case WLS_WORKQUEUE_SELF:
		if (data->work_q)
			destroy_workqueue(data->work_q);
		break;
	default:
		break;
	}

	return 0;
}

/**
 * The "tasklet" task class
 */
static void tasklet_func(unsigned long data)
{
	struct task_class *task = (struct task_class *) data;

	/* do the work */

	/* signal completion */
	task->complete(task->complete_data);
	atomic_set(&task->busy, 0);
}

static struct tasklet_data_s {
	struct task_class *task_ptr;
	struct tasklet_struct tasklet;
} tasklet_data;

int tasklet_initialize(struct task_class *task, void (*complete)(unsigned long))
{
	struct tasklet_data_s *data = task->task_data;

	spin_lock_init(&task->lock);
	tasklet_init(&data->tasklet, tasklet_func, (unsigned long) task);
	data->task_ptr = task;
	task->complete = complete;

	return 0;
}

/* context: interrupt */
int tasklet_invoke(struct task_class *task, unsigned long complete_data)
{
	struct tasklet_data_s *data = task->task_data;
	unsigned long flags;

	BUG_ON(data != &tasklet_data);

	if (atomic_read(&task->busy))
		return -EBUSY;

	/* place the work in queue */
	spin_lock_irqsave(&task->lock, flags);
	atomic_set(&task->busy, 1);
	task->complete_data = complete_data;
	spin_unlock_irqrestore(&task->lock, flags);

	tasklet_schedule(&data->tasklet);

	return 0;
}

/* context: may sleep */
int tasklet_remove(struct task_class *task)
{
	while (atomic_read(&task->busy))
		schedule();
	return 0;
}

/**
 * The "kthread" task class
 */
static struct kthread_data_s {
	struct task_class *task_ptr;
	struct task_struct *thread;
	wait_queue_head_t waitq;
	int has_request, need_exit;
	struct completion done;
} kthread_data;

static int kthread_func(void *arg)
{
	struct task_class *task = (struct task_class *) arg;
	struct kthread_data_s *data = task->task_data;

	for (;;) {
		wait_event(data->waitq, data->has_request || data->need_exit);

		if (data->need_exit) {
			atomic_set(&task->busy, 0);
			break;
		}

		if (data->has_request) {
			/* signal completion */
			data->has_request = 0;
			task->complete(task->complete_data);
			atomic_set(&task->busy, 0);
		}
	}

	complete_and_exit(&data->done, 0);
}

void kthread_print_priority(struct task_struct *thread)
{
	char sched_name[16];
	switch (thread->policy) {
	case SCHED_NORMAL:
		strcpy(sched_name, "sched_normal");
		break;
	case SCHED_RR:
		strcpy(sched_name, "sched_rr");
		break;
	case SCHED_FIFO:
		strcpy(sched_name, "sched_fifo");
		break;
	default:
		snprintf(sched_name, sizeof(sched_name), "%d", thread->policy);
		break;
	}
	/* Thread info */
	printk(KERN_INFO "%s: thread policy: %s\n", DEVICE_NAME, sched_name);
	printk(KERN_INFO "%s: thread prio %d, static_prio %d\n",
			DEVICE_NAME, thread->prio, thread->static_prio);
}

int kthread_init(struct task_class *task, void (*complete)(unsigned long))
{
	struct kthread_data_s *data = task->task_data;

	spin_lock_init(&task->lock);
	init_waitqueue_head(&data->waitq);
	init_completion(&data->done);

	data->need_exit = 0;
	data->task_ptr = task;
	task->complete = complete;

	data->thread = kthread_run((void *)kthread_func, task, DEVICE_NAME);

	if (1)
		kthread_print_priority(data->thread);

	return 0;
}

/* context: interrupt */
int kthread_invoke(struct task_class *task, unsigned long complete_data)
{
	struct kthread_data_s *data = task->task_data;
	unsigned long flags;

	BUG_ON(data != &kthread_data);

	if (atomic_read(&task->busy))
		return -EBUSY;

	/* place the work in queue */
	spin_lock_irqsave(&task->lock, flags);
	atomic_set(&task->busy, 1);
	task->complete_data = complete_data;
	spin_unlock_irqrestore(&task->lock, flags);

	data->has_request = 1;
	wake_up(&data->waitq);

	return 0;
}

/* context: may sleep */
int kthread_remove(struct task_class *task)
{
	struct kthread_data_s *data = task->task_data;
	data->need_exit = 1;

	wake_up(&data->waitq);
	wait_for_completion(&data->done);

	return 0;
}

#define NUM_TASKS ARRAY_SIZE(task_collection)
struct task_class task_collection[WL_TYPE_MAX] = {
		{
				.name = "work",
				.task_data = &work_data,
				.init = work_init,
				.invoke = work_invoke,
				.remove = work_remove,
		},
		{
				.name = "tasklet",
				.task_data = &tasklet_data,
				.init = tasklet_initialize,
				.invoke = tasklet_invoke,
				.remove = tasklet_remove,
		},
		{
				.name = "kthread",
				.task_data = &kthread_data,
				.init = kthread_init,
				.invoke = kthread_invoke,
				.remove = kthread_remove,
		},
};

static void wl_test_complete(unsigned long data)
{
	struct timeval *exec_time = (void *) data;
	do_gettimeofday(exec_time);
}

static int wl_test_init_trace(struct latency_test_class *test)
{
	test->trace = kzalloc(sizeof(struct test_trace_entry) * test->iter_num, GFP_KERNEL);
	if (!test->trace)
		return -ENOMEM;
	return 0;
}

static int wl_test_destroy_trace(struct latency_test_class *test)
{
	if (test->trace)
		kfree(test->trace);
	return 0;
}

static void wl_wait_for_test_complete(struct task_class *task)
{
	while (atomic_read(&task->busy)) {
		schedule();
	}
}

/* currently, just generates the result (usec), returns and printk */
static int wl_gen_results(struct latency_test_class *test,
		struct test_result *test_result)
{
	int i;
	unsigned long usec, max_usec = 0, avg_usec;
	unsigned long sum_usec = 0;
	unsigned long sum_sq_usec = 0, var_usec;

	test_result->err_cnt = 0; /* inc if result appears invalid */

	for (i = 0; i < test->iter_num; i++)
	{
		usec = test->trace[i].exec_time.tv_sec - test->trace[i].invoke_time.tv_sec;
		usec *= 1000000;
		usec += (test->trace[i].exec_time.tv_usec - test->trace[i].invoke_time.tv_usec);

		sum_usec += usec;
		sum_sq_usec += usec * usec;

		if (usec > max_usec)
			max_usec = usec;

		pr_debug("%s: test %d -- delay = %lu us\n", DEVICE_NAME, i, usec);
	}

	avg_usec = sum_usec / test->iter_num;
	var_usec = sum_sq_usec / test->iter_num - avg_usec * avg_usec;

	printk("%s: avg delay = %lu usec\n", DEVICE_NAME, avg_usec);
	printk("%s: max delay = %lu usec\n", DEVICE_NAME, max_usec);

	test_result->n = test->iter_num;
	test_result->avg = avg_usec;
	test_result->max = max_usec;
	test_result->var = var_usec;

	return 0;
}

static int wl_test_run_process(struct latency_test_class *test,
		struct test_result *test_result)
{
	struct task_class *task = &task_collection[test->task_id];
	struct timeval *exec_tv_ptr;
	int i;
	int rc;
	if ((rc = wl_test_init_trace(test)))
		goto fail;

	/* Init task data */
	task->subtype = test->subtype;

	/* Run the test */
	printk("%s: task type: %s subtype: %d\n", DEVICE_NAME,
			task->name, task->subtype);
	if ((rc=task->init(task, wl_test_complete)))
	{
		printk("%s: failed to init task %s, errno = %d\n",
				DEVICE_NAME, task->name, rc);
		goto free_trace;
	}

	for (i = 0; i < test->iter_num; i++)
	{
		pr_debug("%s: i = %d\n", DEVICE_NAME, i);
		exec_tv_ptr = &test->trace[i].exec_time;
		memset(exec_tv_ptr, 0, sizeof(*exec_tv_ptr));
		do_gettimeofday(&test->trace[i].invoke_time);
		if (!task->invoke(task, (unsigned long) exec_tv_ptr))
		{
			/* wait for completion */
			wl_wait_for_test_complete(task);
		}
		else {
			printk("%s: cannot invoke test %d (still busy...)\n",
					DEVICE_NAME, i);
		}
	}

	task->remove(task);

	/* Generate results -- delay in usec */
	rc = wl_gen_results(test, test_result);

free_trace:
	wl_test_destroy_trace(test);
fail:
	return rc;
}

/**
 * Starts a test
 *
 * Each test runs in this way:
 *   1) The wl_test_run function starts the test trigger, which
 *   	can either run in the process context (just a loop), or
 *   	set up some timer or irq handler for other triggers.
 *   2) Before the loop the task_class.init should be called to
 *   	initialize the would-be-tested task
 *   3) For each iteration, the trigger will call task_class.invoke
 *   	with unsigned long data, when the task is woken up, it should
 *   	call the 'complete' function with this data. the framework
 *   	will record the time when the complete function is called.
 *
 * Context: process ioctl call
 */
static int wl_test_run(struct latency_test_class *test,
		struct test_result *test_result)
{
	switch (test->start_type) {
	case WL_START_PROCESS:
		if (test->task_id >= 0 && test->task_id <= NUM_TASKS) {
			return wl_test_run_process(test, test_result);
		}
		break;
	default:
		break;
	}
	return 0;
}

static int wl_open(struct inode *inode, struct file *filp)
{
	struct latency_device *dev; /* device information */
	dev = container_of(inode->i_cdev, struct latency_device, cdev);

	filp->private_data = dev;

	return 0;
}

static int wl_release(struct inode *inode, struct file *filp)
{
	return 0;
}

int wl_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct latency_device *dev = filp->private_data;
	struct test_result test_result;
	int rc;
	switch (cmd) {
		case IOCTL_SELECT_TASK:
			if (arg >= 0 && arg < NUM_TASKS) {
				printk("%s: task type = %d\n", DEVICE_NAME, (int) arg);
				dev->test.task_id = (int) arg;
				dev->test.subtype = WLS_DEFAULT;
			}
			else
				return -EINVAL;
			break;

		case IOCTL_SELECT_SUBTYPE:
			if (arg >= 0 && arg < WLS_MAX) {
				printk("%s: set subtype %d\n", DEVICE_NAME, (int) arg);
				dev->test.subtype = (int) arg;
			}
			else
				return -EINVAL;
			break;

		case IOCTL_SET_ITERATIONS:
			printk("%s: interations = %d\n", DEVICE_NAME, (int) arg);
			dev->test.iter_num = (int) arg;
			break;

		case IOCTL_RUN:
			rc = copy_from_user(&test_result, (void*) arg, sizeof(size_t));
			if (rc || test_result.size != sizeof(test_result))
				return -EINVAL;

			if ((rc = wl_test_run(&dev->test, &test_result)))
				return rc;

			return copy_to_user((void*) arg, &test_result, sizeof(test_result));

		default:
			break;
	}
	return 0;
}

struct file_operations wl_fops = {
		.owner =     THIS_MODULE,
		.open =      wl_open,
		.release =   wl_release,
		.ioctl =     wl_ioctl,
};

/* Initialize the cdev, called by urlstream_init only */
static void wl_setup_cdev(struct latency_device *dev, dev_t devno)
{
	int rc;

	/* cdev */
	cdev_init(&dev->cdev, &wl_fops);
	dev->devno = devno;
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &wl_fops;
	rc = cdev_add(&dev->cdev, dev->devno, 1);
	if (rc) {
		printk(KERN_ERR "%s: cannot add cdev, errno = %d\n",
				DEVICE_NAME, rc);
	}
}

static int wl_init_test(struct latency_device *dev)
{
	dev->test.task_id = DEFAULT_TASK_TYPE;
	dev->test.subtype = WLS_DEFAULT;
	dev->test.iter_num = DEFAULT_KERN_TEST_NUM;
	dev->test.start_type = WL_START_PROCESS;
	return 0;
}

int __init wl_init(void)
{
	int wl_major = DEVICE_MAJOR;
	dev_t devno = MKDEV(wl_major, 0);
	int rc;

	if (wl_major)
		rc = register_chrdev_region(devno, 1, DEVICE_NAME);
	else {
		rc = alloc_chrdev_region(&devno, 0, 1, DEVICE_NAME);
		wl_major = MAJOR(devno);
	}
	if (rc < 0)
		return rc;

	device = kzalloc(sizeof(struct latency_device), GFP_KERNEL);
	if (!device) {
		rc = -ENOMEM;
		goto fail_malloc;
	}

	/* setup char dev */
	wl_setup_cdev(device, devno);

	/* setup device node */
	device->dev_class = class_create(THIS_MODULE, "wake_latency");
	device->device = device_create(device->dev_class, NULL, devno,
			"%s", "wake_latency");

	/* setup test */
	wl_init_test(device);

	printk("%s: ========== LOADED =========\n", DEVICE_NAME);

	return 0;

fail_malloc:
	unregister_chrdev_region(devno, 1);
	return rc;
}

void __exit wl_cleanup(void)
{
	cdev_del(&device->cdev);
	unregister_chrdev_region(device->devno, 1);

	device_destroy(device->dev_class, device->devno);
	class_destroy(device->dev_class);

	kfree(device);
}

module_init(wl_init);
module_exit(wl_cleanup);
MODULE_LICENSE("GPL");
