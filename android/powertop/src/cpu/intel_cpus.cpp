/*
 * Copyright 2010, Intel Corporation
 *
 * This file is part of PowerTOP
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 * or just google for it.
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 */
#include "intel_cpus.h"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "../lib.h"
#include "../parameters/parameters.h"
#include "../display.h"

static int intel_cpu_models[] = {
	0x1A,	/* Core i7, Xeon 5500 series */
	0x1E,	/* Core i7 and i5 Processor - Lynnfield Jasper Forest */
	0x1F,	/* Core i7 and i5 Processor - Nehalem */
	0x2E,	/* Nehalem-EX Xeon */
	0x2F,	/* Westmere-EX Xeon */
	0x25,	/* Westmere */
	0x27,	/* Medfield Atom*/
	0x2C,	/* Westmere */
	0x2A,	/* SNB */
	0x2D,	/* SNB Xeon */
	0x3A,   /* IVB */
	0x3C,
	0x3D,	/* IVB Xeon */
	0x37,	/* BYT-M */
	0x45,	/* HSW-ULT */
	0	/* last entry must be zero */
};

int is_supported_intel_cpu(int model)
{
	int i;

	for (i = 0; intel_cpu_models[i] != 0; i++)
		if (model == intel_cpu_models[i])
			return 1;

	return 0;
}

static uint64_t get_msr(int cpu, uint64_t offset)
{
	ssize_t retval;
	uint64_t msr;

	retval = read_msr(cpu, offset, &msr);
	if (retval < 0) {
		reset_display();
		fprintf(stderr, _("read_msr cpu%d 0x%llx : "), cpu, (unsigned long long)offset);
		fprintf(stderr, "%s\n", strerror(errno));
		exit(-2);
	}

	return msr;
}

nhm_core::nhm_core(int model)
{
	has_c2c7_res = 0;

	switch(model) {
		case 0x2A:	/* SNB */
		case 0x2D:	/* SNB Xeon */
		case 0x3A:      /* IVB */
		case 0x3C:
		case 0x3D:      /* IVB Xeon */
		case 0x45:	/* Next Gen Intel Core Processor */
			has_c2c7_res = 1;
	}

	/* BYT-M does not support C3/C4 */
	if (model == 0x37) {
		has_c3_res = 0;
		has_c1_res = 1;
	} else {
		has_c3_res = 1;
		has_c1_res = 0;
	}
}

void nhm_core::measurement_start(void)
{
	ifstream file;
	char filename[4096];

	/* the abstract function needs to be first since it clears all state */
	abstract_cpu::measurement_start();

	last_stamp = 0;

	if (this->has_c1_res)
		c1_before = get_msr(first_cpu, MSR_CORE_C1_RESIDENCY);
	if (this->has_c3_res)
		c3_before    = get_msr(first_cpu, MSR_CORE_C3_RESIDENCY);
	c6_before    = get_msr(first_cpu, MSR_CORE_C6_RESIDENCY);
	if (this->has_c2c7_res)
		c7_before    = get_msr(first_cpu, MSR_CORE_C7_RESIDENCY);
	tsc_before   = get_msr(first_cpu, MSR_TSC);

	if (this->has_c1_res)
		insert_cstate("core c1", "C1 (cc1)", 0, c1_before, 1);
	if (this->has_c3_res)
		insert_cstate("core c3", "C3 (cc3)", 0, c3_before, 1);
	insert_cstate("core c6", "C6 (cc6)", 0, c6_before, 1);
	if (this->has_c2c7_res) {
		insert_cstate("core c7", "C7 (cc7)", 0, c7_before, 1);
	}


	sprintf(filename, "/sys/devices/system/cpu/cpu%i/cpufreq/stats/time_in_state", first_cpu);

	file.open(filename, ios::in);

	if (file) {
		char line[1024];

		while (file) {
			uint64_t f;
			file.getline(line, 1024);
			f = strtoull(line, NULL, 10);
			account_freq(f, 0);
		}
		file.close();
	}
	account_freq(0, 0);

}

void nhm_core::measurement_end(void)
{
	unsigned int i;
	uint64_t time_delta;
	double ratio;

	if (this->has_c1_res)
		c1_after = get_msr(first_cpu, MSR_CORE_C1_RESIDENCY);
	if (this->has_c3_res)
		c3_after    = get_msr(first_cpu, MSR_CORE_C3_RESIDENCY);
	c6_after    = get_msr(first_cpu, MSR_CORE_C6_RESIDENCY);
	if (this->has_c2c7_res)
		c7_after    = get_msr(first_cpu, MSR_CORE_C7_RESIDENCY);
	tsc_after   = get_msr(first_cpu, MSR_TSC);

	if (this->has_c1_res)
		finalize_cstate("core c1", 0, c1_after, 1);
	if (this->has_c3_res)
		finalize_cstate("core c3", 0, c3_after, 1);
	finalize_cstate("core c6", 0, c6_after, 1);
	if (this->has_c2c7_res)
		finalize_cstate("core c7", 0, c7_after, 1);

	gettimeofday(&stamp_after, NULL);

	time_factor = 1000000.0 * (stamp_after.tv_sec - stamp_before.tv_sec) + stamp_after.tv_usec - stamp_before.tv_usec;

	for (i = 0; i < children.size(); i++)
		if (children[i]) {
			children[i]->measurement_end();
			children[i]->wiggle();
		}

	time_delta = 1000000 * (stamp_after.tv_sec - stamp_before.tv_sec) + stamp_after.tv_usec - stamp_before.tv_usec;

	ratio = 1.0 * time_delta / (tsc_after - tsc_before);

	for (i = 0; i < cstates.size(); i++) {
		struct idle_state *state = cstates[i];

		if (state->after_count == 0) {
			cout << "after count is 0\n";
			continue;
		}

		if (state->after_count != state->before_count) {
			cout << "count mismatch\n";
			continue;
		}

		state->usage_delta =    ratio * (state->usage_after    - state->usage_before)    / state->after_count;
		state->duration_delta = ratio * (state->duration_after - state->duration_before) / state->after_count;
	}

#if 0
	for (i = 0; i < children.size(); i++)
		if (children[i]) {
			for (j = 0; j < children[i]->pstates.size(); j++) {
				struct frequency *state;
				state = children[i]->pstates[j];
				if (!state)
					continue;

				update_pstate(  state->freq, state->human_name, state->time_before, state->before_count);
				finalize_pstate(state->freq,                    state->time_after,  state->after_count);
			}
		}
#endif
	total_stamp = 0;
}

char * nhm_core::fill_pstate_line(int line_nr, char *buffer)
{
	buffer[0] = 0;
	unsigned int i;

	if (total_stamp ==0) {
		for (i = 0; i < pstates.size(); i++)
			total_stamp += pstates[i]->time_after;
		if (total_stamp == 0)
			total_stamp = 1;
	}

	if (line_nr == LEVEL_HEADER) {
		sprintf(buffer,_("  Core"));
		return buffer;
	}

	if (line_nr >= (int)pstates.size() || line_nr < 0)
		return buffer;

	sprintf(buffer," %5.1f%% ", percentage(1.0* (pstates[line_nr]->time_after) / total_stamp));
	return buffer;
}

nhm_package::nhm_package(int model)
{
	has_c8c9c10_res = 0;
	has_c2c7_res = 0;

	switch(model) {
		case 0x2A:	/* SNB */
		case 0x2D:	/* SNB Xeon */
		case 0x3A:      /* IVB */
		case 0x3C:
		case 0x37:
		case 0x3D:      /* IVB Xeon */
		case 0x45:
			has_c2c7_res = 1;
	}

	/* BYT-M doesn't have C3 */
	if (model == 0x37)
		has_c3_res = 0;
	else
		has_c3_res = 1;

	/* Haswell-ULT has C8/9/10*/
	if (model == 0x45)
		has_c8c9c10_res = 1;
}

char * nhm_package::fill_pstate_line(int line_nr, char *buffer)
{
	buffer[0] = 0;
	unsigned int i;

	if (total_stamp ==0) {
		for (i = 0; i < pstates.size(); i++)
			total_stamp += pstates[i]->time_after;
		if (total_stamp == 0)
			total_stamp = 1;
	}


	if (line_nr == LEVEL_HEADER) {
		sprintf(buffer,_("  Package"));
		return buffer;
	}

	if (line_nr >= (int)pstates.size() || line_nr < 0)
		return buffer;

	sprintf(buffer," %5.1f%% ", percentage(1.0* (pstates[line_nr]->time_after) / total_stamp));
	return buffer;
}



void nhm_package::measurement_start(void)
{
	abstract_cpu::measurement_start();

	last_stamp = 0;

	if (this->has_c2c7_res)
		c2_before    = get_msr(number, MSR_PKG_C2_RESIDENCY);

	if (this->has_c3_res)
		c3_before    = get_msr(number, MSR_PKG_C3_RESIDENCY);
	c6_before    = get_msr(number, MSR_PKG_C6_RESIDENCY);
	if (this->has_c2c7_res)
		c7_before    = get_msr(number, MSR_PKG_C7_RESIDENCY);
	if (this->has_c8c9c10_res) {
		c8_before    = get_msr(number, MSR_PKG_C8_RESIDENCY);
		c9_before    = get_msr(number, MSR_PKG_C9_RESIDENCY);
		c10_before    = get_msr(number, MSR_PKG_C10_RESIDENCY);
	}
	tsc_before   = get_msr(first_cpu, MSR_TSC);

	if (this->has_c2c7_res)
		insert_cstate("pkg c2", "C2 (pc2)", 0, c2_before, 1);

	if (this->has_c3_res)
		insert_cstate("pkg c3", "C3 (pc3)", 0, c3_before, 1);
	insert_cstate("pkg c6", "C6 (pc6)", 0, c6_before, 1);
	if (this->has_c2c7_res)
		insert_cstate("pkg c7", "C7 (pc7)", 0, c7_before, 1);
	if (this->has_c8c9c10_res) {
		insert_cstate("pkg c8", "C8 (pc8)", 0, c8_before, 1);
		insert_cstate("pkg c9", "C9 (pc9)", 0, c9_before, 1);
		insert_cstate("pkg c10", "C10 (pc10)", 0, c10_before, 1);
	}
}

void nhm_package::measurement_end(void)
{
	uint64_t time_delta;
	double ratio;
	unsigned int i, j;

	for (i = 0; i < children.size(); i++)
		if (children[i])
			children[i]->wiggle();


	if (this->has_c2c7_res)
		c2_after    = get_msr(number, MSR_PKG_C2_RESIDENCY);

	if (this->has_c3_res)
		c3_after    = get_msr(number, MSR_PKG_C3_RESIDENCY);
	c6_after    = get_msr(number, MSR_PKG_C6_RESIDENCY);
	if (this->has_c2c7_res)
		c7_after    = get_msr(number, MSR_PKG_C7_RESIDENCY);
	if (has_c8c9c10_res) {
		c8_after = get_msr(number, MSR_PKG_C8_RESIDENCY);
		c9_after = get_msr(number, MSR_PKG_C9_RESIDENCY);
		c10_after = get_msr(number, MSR_PKG_C10_RESIDENCY);
	}
	tsc_after   = get_msr(first_cpu, MSR_TSC);

	gettimeofday(&stamp_after, NULL);

	time_factor = 1000000.0 * (stamp_after.tv_sec - stamp_before.tv_sec) + stamp_after.tv_usec - stamp_before.tv_usec;


	if (this->has_c2c7_res)
		finalize_cstate("pkg c2", 0, c2_after, 1);

	if (this->has_c3_res)
		finalize_cstate("pkg c3", 0, c3_after, 1);
	finalize_cstate("pkg c6", 0, c6_after, 1);
	if (this->has_c2c7_res)
		finalize_cstate("pkg c7", 0, c7_after, 1);
	if (has_c8c9c10_res) {
		finalize_cstate("pkg c8", 0, c8_after, 1);
		finalize_cstate("pkg c9", 0, c9_after, 1);
		finalize_cstate("pkg c10", 0, c10_after, 1);
	}

	for (i = 0; i < children.size(); i++)
		if (children[i])
			children[i]->measurement_end();

	time_delta = 1000000 * (stamp_after.tv_sec - stamp_before.tv_sec) + stamp_after.tv_usec - stamp_before.tv_usec;

	ratio = 1.0 * time_delta / (tsc_after - tsc_before);


	for (i = 0; i < cstates.size(); i++) {
		struct idle_state *state = cstates[i];

		if (state->after_count == 0) {
			cout << "after count is 0\n";
			continue;
		}

		if (state->after_count != state->before_count) {
			cout << "count mismatch\n";
			continue;
		}

		state->usage_delta =    ratio * (state->usage_after    - state->usage_before)    / state->after_count;
		state->duration_delta = ratio * (state->duration_after - state->duration_before) / state->after_count;
	}
	for (i = 0; i < children.size(); i++)
		if (children[i]) {
			for (j = 0; j < children[i]->pstates.size(); j++) {
				struct frequency *state;
				state = children[i]->pstates[j];
				if (!state)
					continue;

				update_pstate(  state->freq, state->human_name, state->time_before, state->before_count);
				finalize_pstate(state->freq,                    state->time_after,  state->after_count);
			}
		}
	total_stamp = 0;

}

void nhm_cpu::measurement_start(void)
{
	ifstream file;
	char filename[4096];

	cpu_linux::measurement_start();

	last_stamp = 0;

	aperf_before = get_msr(number, MSR_APERF);
	mperf_before = get_msr(number, MSR_MPERF);
	tsc_before   = get_msr(number, MSR_TSC);

	insert_cstate("active", _("C0 active"), 0, aperf_before, 1);

	sprintf(filename, "/sys/devices/system/cpu/cpu%i/cpufreq/stats/time_in_state", first_cpu);

	file.open(filename, ios::in);

	if (file) {
		char line[1024];

		while (file) {
			uint64_t f;
			file.getline(line, 1024);
			f = strtoull(line, NULL, 10);
			account_freq(f, 0);
		}
		file.close();
	}
	account_freq(0, 0);
}

void nhm_cpu::measurement_end(void)
{
	uint64_t time_delta;
	double ratio;
	unsigned int i;

	aperf_after = get_msr(number, MSR_APERF);
	mperf_after = get_msr(number, MSR_MPERF);
	tsc_after   = get_msr(number, MSR_TSC);



	finalize_cstate("active", 0, aperf_after, 1);


	cpu_linux::measurement_end();

	time_delta = 1000000 * (stamp_after.tv_sec - stamp_before.tv_sec) + stamp_after.tv_usec - stamp_before.tv_usec;

	ratio = 1.0 * time_delta / (tsc_after - tsc_before);


	for (i = 0; i < cstates.size(); i++) {
		struct idle_state *state = cstates[i];
		if (state->line_level != LEVEL_C0)
			continue;

		state->usage_delta =    ratio * (state->usage_after    - state->usage_before)    / state->after_count;
		state->duration_delta = ratio * (state->duration_after - state->duration_before) / state->after_count;
	}

	total_stamp = 0;

}


char * nhm_cpu::fill_pstate_name(int line_nr, char *buffer)
{
	if (line_nr == LEVEL_C0) {
		sprintf(buffer, _("Actual"));
		return buffer;
	}
	return cpu_linux::fill_pstate_name(line_nr, buffer);
}

char * nhm_cpu::fill_pstate_line(int line_nr, char *buffer)
{
	if (total_stamp ==0) {
		unsigned int i;
		for (i = 0; i < pstates.size(); i++)
			total_stamp += pstates[i]->time_after;
		if (total_stamp == 0)
			total_stamp = 1;
	}

	if (line_nr == LEVEL_HEADER) {
		sprintf(buffer,_(" CPU %i"), number);
		return buffer;
	}

	if (line_nr == LEVEL_C0) {
		double F;
		F = 1.0 * (tsc_after - tsc_before) * (aperf_after - aperf_before) / (mperf_after - mperf_before) / time_factor * 1000;
		hz_to_human(F, buffer, 1);
		return buffer;
	}
	if (line_nr >= (int)pstates.size() || line_nr < 0)
		return buffer;

	sprintf(buffer," %5.1f%% ", percentage(1.0* (pstates[line_nr]->time_after) / total_stamp));
	return buffer;

}


int nhm_cpu::has_pstate_level(int level)
{
	if (level == LEVEL_C0)
		return 1;
	return cpu_linux::has_pstate_level(level);
}
