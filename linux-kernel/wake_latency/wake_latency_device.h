/**
 * Wake_latency header file shared between the module and the front-end:
 * wl_test.c
 *
 * License: BSD / Freeware
 * History:
 *   2010/07/24		Danke Xie		Initial version
 */

#ifndef WAKE_LATENCY_DEVICE_H_
#define WAKE_LATENCY_DEVICE_H_

#define DEVICE_MAJOR 0     /* dynamic major by default */
#define DEVICE_MAGIC 240

#define DEFAULT_KERN_TEST_NUM	10000
#define DEFAULT_USER_TEST_NUM	100

#define TASK_NAME_MAXLEN 	64

enum task_type {
	WL_TYPE_WORKQUEUE = 0,
	WL_TYPE_TASKLET,
	WL_TYPE_KTHREAD,
	WL_TYPE_MAX
};

/* optionally set one of the subtypes before running a test */
enum task_subtype {
	WLS_DEFAULT = 0,
	WLS_WORKQUEUE_GLOBAL,   /* use global work queue */
	WLS_WORKQUEUE_SELF,		/* use local work queue */
	WLS_MAX
};

enum test_start_type {
	WL_START_PROCESS,   /* start the test in the caller process */
	WL_START_TIMER,		/* within a timer context, not supported yet */
	WL_START_IRQ,		/* hook up some irq to do so, not supported yet */
};

enum ioctl_cmd_type {
	WL_IOCTL_RUN = 1,
	WL_IOCTL_SELECT_TASK,
	WL_IOCTL_SELECT_SUBTYPE,
	WL_IOCTL_SET_START_TYPE,
	WL_IOCTL_SET_ITERATIONS,
};

struct test_result {
	uint32_t size;
	uint32_t n;
	uint32_t avg;
	uint32_t max;
	uint64_t var; 		/* sqr of std. dev. */
	uint32_t err_cnt; 	/* invalid data */
};

#define IOCTL_RUN _IOW(DEVICE_MAGIC, WL_IOCTL_RUN, struct test_result *)
#define IOCTL_SELECT_TASK _IOR(DEVICE_MAGIC, WL_IOCTL_SELECT_TASK, int)
#define IOCTL_SELECT_SUBTYPE _IOR(DEVICE_MAGIC, WL_IOCTL_SELECT_SUBTYPE, int)
#define IOCTL_SET_START_TYPE _IOR(DEVICE_MAGIC, WL_IOCTL_SET_START_TYPE, int)
#define IOCTL_SET_ITERATIONS _IOR(DEVICE_MAGIC, WL_IOCTL_SET_ITERATIONS, int)

#endif /* WAKE_LATENCY_DEVICE_H_ */
