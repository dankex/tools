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

#include "device.h"
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;

#include "backlight.h"
#include "usb.h"
#include "ahci.h"
#include "alsa.h"
#include "rfkill.h"
#include "i915-gpu.h"
#include "thinkpad-fan.h"
#include "thinkpad-light.h"
#include "network.h"
#include "runtime_pm.h"

#include "../parameters/parameters.h"
#include "../display.h"
#include "../lib.h"
#include "../report/report.h"
#include "../report/report-maker.h"
#include "../measurement/measurement.h"
#include "../devlist.h"
#include <unistd.h>

device::device(void)
{
	cached_valid = 0;
	hide = 0;

	memset(guilty, 0, sizeof(guilty));
	memset(real_path, 0, sizeof(real_path));
}


void device::register_sysfs_path(const char *path)
{
	char current_path[PATH_MAX + 1];
	int iter = 0;
	strcpy(current_path, path);

	while (iter++ < 10) {
		char test_path[PATH_MAX + 1];
		sprintf(test_path, "%s/device", current_path);
		if (access(test_path, R_OK) == 0)
			strcpy(current_path, test_path);
		else
			break;
	}

	if (!realpath(current_path, real_path))
		real_path[0] = 0;
}

void device::start_measurement(void)
{
	hide = false;
}

void device::end_measurement(void)
{
}

double	device::utilization(void)
{
	return 0.0;
}



vector<class device *> all_devices;


void devices_start_measurement(void)
{
	unsigned int i;
	for (i = 0; i < all_devices.size(); i++)
		all_devices[i]->start_measurement();
}

void devices_end_measurement(void)
{
	unsigned int i;
	for (i = 0; i < all_devices.size(); i++)
		all_devices[i]->end_measurement();

	clear_devpower();

	for (i = 0; i < all_devices.size(); i++) {
		all_devices[i]->hide = false;
		all_devices[i]->register_power_with_devlist(&all_results, &all_parameters);
	}
}

static bool power_device_sort(class device * i, class device * j)
{
	double pI, pJ;
	pI = i->power_usage(&all_results, &all_parameters);
	pJ = j->power_usage(&all_results, &all_parameters);

	if (equals(pI, pJ)) {
		int vI, vJ;
		vI = i->power_valid();
		vJ = j->power_valid();

		if (vI != vJ)
			return vI > vJ;

		return i->utilization() > j->utilization();
	}
	return pI > pJ;
}


void report_devices(void)
{
	WINDOW *win;
	unsigned int i;
	int show_power;
	double pw;

	char util[128];
	char power[128];

	win = get_ncurses_win("Device stats");
        if (!win)
                return;

	show_power = global_power_valid();

        wclear(win);
        wmove(win, 2,0);

	sort(all_devices.begin(), all_devices.end(), power_device_sort);



	pw = global_joules_consumed();
	if (pw > 0.0001) {
		char buf[32];
		wprintw(win, _("The battery reports a discharge rate of %sW\n"),
				fmt_prefix(pw, buf));
	}

	if (show_power) {
		char buf[32];
		wprintw(win, _("System baseline power is estimated at %sW\n"),
				fmt_prefix(get_parameter_value("base power"), buf));
	}

	if (pw > 0.0001 || show_power)
		wprintw(win, "\n");
	if (show_power)
		wprintw(win, _("Power est.    Usage     Device name\n"));
	else
		wprintw(win, _("              Usage     Device name\n"));

	for (i = 0; i < all_devices.size(); i++) {
		double P;

		util[0] = 0;

		if (all_devices[i]->util_units()) {
			if (all_devices[i]->utilization() < 1000)
				sprintf(util, "%5.1f%s",  all_devices[i]->utilization(),  all_devices[i]->util_units());
			else
				sprintf(util, "%5i%s",  (int)all_devices[i]->utilization(),  all_devices[i]->util_units());
		}
		while (strlen(util) < 13) strcat(util, " ");

		P = all_devices[i]->power_usage(&all_results, &all_parameters);

		format_watts(P, power, 11);

		if (!show_power || !all_devices[i]->power_valid())
			strcpy(power, "           ");


		wprintw(win, "%s %s %s\n",
			power,
			util,
			all_devices[i]->human_name()
			);
	}
}

void show_report_devices(void)
{
	unsigned int i;
	int show_power;
	double pw;

	show_power = global_power_valid();
	sort(all_devices.begin(), all_devices.end(), power_device_sort);

	report.begin_section(SECTION_DEVPOWER);
	report.add_header("Device Power Report");

	pw = global_joules_consumed();
	if (pw > 0.0001) {
		char buf[32];

		report.begin_paragraph();
		report.addf("The battery reports a discharge rate of %sW",
			    fmt_prefix(pw, buf));
	}

	if (show_power) {
		char buf[32];

		report.begin_paragraph();
		report.addf("System baseline power is estimated at %sW",
			    fmt_prefix(get_parameter_value("base power"), buf));
	}

	report.begin_table(TABLE_WIDE);
	report.begin_row();
	if (show_power) {
		report.begin_cell(CELL_DEVPOWER_HEADER);
		report.add("Power est.");
	}

	report.begin_cell(CELL_DEVPOWER_HEADER);
	report.add("Usage");
	report.begin_cell(CELL_DEVPOWER_DEV_NAME);
	report.add("Device name");

	for (i = 0; i < all_devices.size(); i++) {
		double P;
		char util[128];
		char power[128];

		util[0] = 0;
		if (all_devices[i]->util_units()) {
			if (all_devices[i]->utilization() < 1000)
				sprintf(util, "%5.1f%s",
					all_devices[i]->utilization(),
					all_devices[i]->util_units());
			else
				sprintf(util, "%5i%s",
					(int)all_devices[i]->utilization(),
					all_devices[i]->util_units());
		}

		P = all_devices[i]->power_usage(&all_results, &all_parameters);
		format_watts(P, power, 11);

		if (!show_power || !all_devices[i]->power_valid())
			strcpy(power, "           ");

		report.begin_row(ROW_DEVPOWER);
		if (show_power) {
			report.begin_cell(CELL_DEVPOWER_POWER);
			report.add(power);
		}

		report.begin_cell(CELL_DEVPOWER_UTIL);
		report.add(util);
		report.begin_cell();
		report.add(all_devices[i]->human_name());
	}
}


void create_all_devices(void)
{
	create_all_backlights();
	create_all_usb_devices();
	create_all_ahcis();
	create_all_alsa();
	create_all_rfkills();
	create_i915_gpu();
	create_thinkpad_fan();
	create_thinkpad_light();
	create_all_nics();
	create_all_runtime_pm_devices();
}


void clear_all_devices(void)
{
	unsigned int i;
	for (i = 0; i < all_devices.size(); i++) {
		delete all_devices[i];
	}
	all_devices.clear();
}
