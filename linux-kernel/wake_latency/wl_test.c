/**
 * A front-end of the kernel module wake_latency.c for measuring scheduling
 * or wake-up latencies of different kernel tasks
 *
 * Usage: ./wl_test <number of load threads>
 *
 * License: BSD / Freeware
 * History:
 *
 *   2010/07/24		Danke Xie		Initial version
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/time.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "wake_latency_device.h"

#define DEV_NAME "/dev/wake_latency"
#define APP_NAME "wl_test"

#define ARRAY_SIZE(x) ( (int) (sizeof(x) / sizeof(*(x))) )

#define DEFAULT_CPU_LOAD 	50		/* number of load threads */

static const int verbose = 1; /* 0 or 1, for verbose run info */

struct cpu_load_item {
	pthread_t th;
	int exit;
	int id;
};

struct test_entry {
	char name[TASK_NAME_MAXLEN];
	enum task_type task;
	enum task_subtype subtype;
};

/* Kernel tests to run */
static struct test_entry tests[] = {
		{"workqueue (global)", WL_TYPE_WORKQUEUE, WLS_WORKQUEUE_GLOBAL},
		{"workqueue (self)", WL_TYPE_WORKQUEUE, WLS_WORKQUEUE_SELF},
		{"tasklet", WL_TYPE_TASKLET, WLS_DEFAULT},
		{"thread", WL_TYPE_KTHREAD, WLS_DEFAULT},
};

/* for kernel tests */
static struct cpu_load_item *cpu_load;
static int g_num_threads;
static pthread_attr_t th_attr_background, th_attr_work;

/* for userspace latency */
static struct timeval invoke_time, exec_time;

static int num_sample = 0;
unsigned long sum_usec = 0, sum_sq_usec = 0, max_usec = 0;

sem_t wait_invoke, done_exec;
pthread_t user_thread;
int latency_thread_exit = 0;

void thread_print_priority(const char *name, int sched_policy, int priority)
{
	char sched_name[16];
	switch (sched_policy) {
	case SCHED_OTHER:
		strcpy(sched_name, "SCHED_OTHER");
		break;
	case SCHED_RR:
		strcpy(sched_name, "SCHED_RR");
		break;
	case SCHED_FIFO:
		strcpy(sched_name, "SCHED_FIFO");
		break;
	default:
		snprintf(sched_name, sizeof(sched_name), "%d", sched_policy);
		break;
	}
	/* Thread info */
	printf("%s thread policy: %s  static_prio %d\n",
			name, sched_name, priority);
}

/* Sets scope, scheduling policy and priority of the background threads */
void set_thread_priority(pthread_attr_t *attr,
		const char *name,
		const int sched_policy,
		const int priority)
{
	/* for native posix thread library, system is the only supported scope */
	const int is_scope_system = 1;

	/*
		SCHED_OTHER	0    --- conventional process, priority from 0 to 0
		SCHED_FIFO	1	 --- real time process, priority from 1 to 99
		SCHED_RR	2    --- real time process, priority from 1 to 99
	*/
	struct sched_param param = { priority };

	pthread_attr_setscope(attr, is_scope_system ?
			PTHREAD_SCOPE_SYSTEM : PTHREAD_SCOPE_PROCESS);

	/* select scheduler and priority */
	pthread_attr_setschedpolicy(attr, sched_policy);
	pthread_attr_setschedparam(attr, &param);

	if (1)
		thread_print_priority(name, sched_policy, priority);
}

/* Set Load thread priority here */
void set_load_thread_priority(pthread_attr_t *attr) {
	const int sched_policy = SCHED_FIFO;
	const int priority = 40; /* larger number higher priority */
	set_thread_priority(attr, "Load", sched_policy, priority);
}

/* Set Work/Test thread priority here */
void set_work_thread_priority(pthread_attr_t *attr) {
	const int sched_policy = SCHED_OTHER;
	const int priority = 0; /* larger number higher priority */
	set_thread_priority(attr, "Test", sched_policy, priority);
}

void *latency_thread(void * parg)
{
	unsigned long usec;

	sem_post(&done_exec);

	while (!latency_thread_exit) {
		sem_wait(&wait_invoke);
		if (latency_thread_exit) break;

		gettimeofday(&exec_time, NULL);

		usec = exec_time.tv_sec - invoke_time.tv_sec;
		usec *= 1000000;
		usec += exec_time.tv_usec - invoke_time.tv_usec;

		sum_usec += usec;
		sum_sq_usec += usec * usec;

		if (usec > max_usec)
			max_usec = usec;

		num_sample++;

		sem_post(&done_exec);
	}

	return NULL;
}

void *load_thread(void *parg)
{
	volatile struct cpu_load_item *cpu_load = parg;
	int a = 179, b = 983, c;

	while (!cpu_load->exit)
	{
		/* just some junk multiplication */
		c = a * b;
		a = b; b = c;
	}

	return NULL;
}

int start_load(int num_threads)
{
	int i;

	cpu_load = malloc( sizeof(struct cpu_load_item) * num_threads);

	if (!cpu_load)
		return -ENOMEM;

	g_num_threads = num_threads;

	for (i = 0; i < g_num_threads; i++)
	{
		cpu_load[i].exit = 0;
		cpu_load[i].id = i;
		if (pthread_create(&cpu_load[i].th, &th_attr_background, load_thread, &cpu_load[i])) {
			printf("thread %d didn't start\n", i);
			cpu_load[i].th = (pthread_t) 0;
		}
	}

	return 0;
}

int end_load()
{
	int i;

	for (i = 0; i < g_num_threads; i++)
		cpu_load[i].exit= 1;

	for (i = 0; i < g_num_threads; i++) {
		if (cpu_load[i].th)
			pthread_join(cpu_load[i].th, (void**) NULL);
	}

	if (cpu_load)
		free(cpu_load);

	return 0;
}

int main(int argc, char *argv[])
{
	int cpuload = DEFAULT_CPU_LOAD;
	struct test_result test_result;
	int fd;
	int i;
	int rc;

	if (argc >= 2)
	{
		cpuload = atoi(argv[1]);
	}

	fd = open(DEV_NAME, O_RDWR);
	if (fd == -1) {
		printf("%s: cannot open device %s\n", APP_NAME, DEV_NAME);
		return -1;
	}

	/* attributes of the work thread */
	pthread_attr_init(&th_attr_work);
	pthread_attr_setdetachstate(&th_attr_work, PTHREAD_CREATE_JOINABLE);
	set_work_thread_priority(&th_attr_work);

	/* attributes of the background load thread(s) */
	pthread_attr_init(&th_attr_background);
	pthread_attr_setdetachstate(&th_attr_background, PTHREAD_CREATE_JOINABLE);
	set_load_thread_priority(&th_attr_background);

	/* start load */
	printf("Start load, %d threads\n", cpuload);
	if ((rc=start_load(cpuload))) {
		printf("Failed to start load, err = %d\n", rc);
	}

	/* run the kernel tests specified in tests[] */
	for (i = 0; i < ARRAY_SIZE(tests); i++)
	{
		if ((rc = ioctl(fd, IOCTL_SELECT_TASK, tests[i].task))) {
			printf("failed to set task: %d, rc=%d\n",
					tests[i].task, rc);
			continue;
		}

		if ((rc = ioctl(fd, IOCTL_SELECT_SUBTYPE, tests[i].subtype)))
		{
			printf("failed to set subtype: %d, rc=%d\n",
					tests[i].subtype, rc);
			continue;
		}

		printf("Start test %d: %s\n", i, tests[i].name);

		test_result.size = sizeof(test_result);
		rc = ioctl(fd, IOCTL_RUN, &test_result);
		if (!rc) {
			printf("N = %d\n", test_result.n);
			printf("Avg delay = %d us\n", test_result.avg);
			printf("Std Dev = %.3f us\n", sqrt(test_result.var));
			printf("Max delay = %d us\n", test_result.max);
		}
		else {
			printf("Test error: %d\n", rc);
		}
		printf("\n");
	}

	close(fd);

	/* userspace testing */
	sem_init(&wait_invoke, 0, 0);
	sem_init(&done_exec, 0, 0);

	if (pthread_create(&user_thread, &th_attr_work, latency_thread, NULL)) {
		printf("Cannot create userspace thread\n");
		user_thread = (pthread_t) 0;
	}
	else {
		sum_usec = 0; num_sample = 0;

		printf("Start test %d: %s\n", i, "userspace");
		sem_wait(&done_exec);
		for (i = 0; i < DEFAULT_USER_TEST_NUM; i++)
		{
			if (verbose) {
				if (i % 10 == 0) {
					printf("%d\t", i);
					fflush(stdout);
				}
			}
			gettimeofday(&invoke_time, NULL);
			sem_post(&wait_invoke);
			sem_wait(&done_exec);
		}
		if (verbose)
			printf("\n");

		printf("N = %d\n", i);
		printf("Avg delay = %lu us\n", (unsigned long) (sum_usec / num_sample));
		printf("Std Dev = %.3f us\n",
				sqrt(sum_sq_usec/num_sample -
						(sum_usec/num_sample) * (sum_usec/num_sample)) );
		printf("Max delay = %lu us\n", max_usec);
		printf("\n");
	}

	/* unload */
	printf("Unloading...\n");

	latency_thread_exit = 1;
	sem_post(&wait_invoke);
	end_load();

	if (user_thread) {
		pthread_join(user_thread, NULL);
	}

	pthread_attr_destroy(&th_attr_work);
	pthread_attr_destroy(&th_attr_background);

	return 0;
}
