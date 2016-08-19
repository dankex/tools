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

#include "tuning.h"
#include "tunable.h"
#include "unistd.h"
#include "tuningsysfs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>


#include "../lib.h"

sysfs_tunable::sysfs_tunable(const char *str, const char *_sysfs_path, const char *_target_content) : tunable(str, 1.0, _("Good"), _("Bad"), _("Unknown"))
{
	strcpy(sysfs_path, _sysfs_path);
	strcpy(target_value, _target_content);
	bad_value[0] = 0;
	sprintf(toggle_good, "echo '%s' > '%s';", target_value, sysfs_path);
	sprintf(toggle_bad, "echo '%s' > '%s';", bad_value, sysfs_path);
}

int sysfs_tunable::good_bad(void)
{
	char current_value[4096], *c;
	ifstream file;

	file.open(sysfs_path, ios::in);
	if (!file)
		return TUNE_NEUTRAL;
	file.getline(current_value, 4096);
	file.close();

	c = strchr(current_value, '\n');
	if (c)
		*c = 0;

	if (strcmp(current_value, target_value) == 0)
		return TUNE_GOOD;

	strcpy(bad_value, current_value);
	return TUNE_BAD;
}

void sysfs_tunable::toggle(void)
{
	int good;
	good = good_bad();

	if (good == TUNE_GOOD) {
		if (strlen(bad_value) > 0)
			write_sysfs(sysfs_path, bad_value);
		return;
	}

	write_sysfs(sysfs_path, target_value);
}

const char *sysfs_tunable::toggle_script(void) {
	int good;
	good = good_bad();

	if (good == TUNE_GOOD) {
		if (strlen(bad_value) > 0)
			return toggle_bad;
		return NULL;
	}

	return toggle_good;
}


void add_sysfs_tunable(const char *str, const char *_sysfs_path, const char *_target_content)
{
	class sysfs_tunable *tunable;

	if (access(_sysfs_path, R_OK) != 0)
		return;

	tunable = new class sysfs_tunable(str, _sysfs_path, _target_content);


	all_tunables.push_back(tunable);
}

void add_sata_tunables(void)
{
	struct dirent *entry;
	DIR *dir;
	char filename[4096];
	char msg[4096];

	dir = opendir("/sys/class/scsi_host");

	if (!dir)
		return;

        while (1) {
		entry = readdir(dir);

		if (!entry)
			break;

		if (entry->d_name[0] == '.')
			continue;

		sprintf(filename, "/sys/class/scsi_host/%s/link_power_management_policy", entry->d_name);

	        sprintf(msg, _("Enable SATA link power Managmenet for %s"),entry->d_name);

		add_sysfs_tunable(msg, filename,"min_power");

        }

        closedir(dir);
}
