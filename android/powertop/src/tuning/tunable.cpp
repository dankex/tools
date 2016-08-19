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
#include <string.h>

vector<class tunable *> all_tunables;
vector<class tunable *> all_untunables;


tunable::tunable(const char *str, double _score, const char *good, const char *bad, const char *neutral)
{
	score = _score;
	strcpy(desc, str);
	strcpy(good_string, good);
	strcpy(bad_string, bad);
	strcpy(neutral_string, neutral);
}


tunable::tunable(void)
{
	score = 0;
	desc[0] = 0;
	strcpy(good_string, _("Good"));
	strcpy(bad_string, _("Bad"));
	strcpy(neutral_string, _("Unknown"));
}
