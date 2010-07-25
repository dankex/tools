/**
 * Header file for the kernel module wake_latency.c
 * License: GPL
 *
 * History:
 *   2010/07/24		Danke Xie		Initial version
 */

#ifndef WAKE_LATENCY_H
#define WAKE_LATENCY_H

#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include "wake_latency_device.h"

#define DEVICE_NAME "wake_latency"

#define CMD_MAXLEN 512
#define MSG_MAXLEN 1024

#define DEFAULT_TASK_TYPE	WL_TYPE_WORKQUEUE

#define __UNUSED __attribute__((unused))

enum state_type {
	WL_STATE_IDLE = 0,
	WL_STATE_RUNNING
};

struct test_trace_entry {
	struct timeval invoke_time;
	struct timeval exec_time;
};

struct latency_test_class {
	int iter_num;
	int task_id;
	enum task_subtype subtype;
	enum test_start_type start_type;
	struct test_trace_entry *trace;
};

struct latency_device {
	struct cdev cdev;
	struct latency_test_class test;
	struct class *dev_class;
	struct device *device;
	dev_t devno;
};

/**
 * Each type of task should define the following operations.
 *
 * @complete  - a callback function when a task is done
 * @init      - this should alloc resource for the task
 * @invoke    - schedules or wakes up the task to run
 * @remove    - stops the task and releases resources
 */
struct task_class {
	char name[TASK_NAME_MAXLEN];
	enum task_subtype subtype; /* copied from latency_test_class */
	spinlock_t lock;
	atomic_t busy;
	void *task_data;
	unsigned long complete_data;
	void (*complete)(unsigned long);
	int (*init)(struct task_class *task, void (*complete)(unsigned long));
	int (*invoke)(struct task_class *task, unsigned long data);
	int (*remove)(struct task_class *task);
};

#endif /* WAKE_LATENCY_H */
