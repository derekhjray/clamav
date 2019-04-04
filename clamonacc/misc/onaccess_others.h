/*
 *  Copyright (C) 2017-2019 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 *  Authors: Mickey Sola
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#ifndef __CLAMD_ONAS_OTHERS_H
#define __CLAMD_ONAS_OTHERS_H

#include "shared/optparser.h"
#include "libclamav/clamav.h"
#include "../clamonacc.h"

typedef enum {
    CHK_CLEAN,
    CHK_FOUND,
    CHK_SELF
} cli_check_t;

int onas_fan_checkowner(int pid, const struct optstruct *opts);
int onas_scan(struct onas_context **ctx, const char *fname, STATBUF sb, int *infected, int *err, cl_error_t *ret_code);
int onas_scan_safe(struct onas_context **ctx, const char *fname, STATBUF sb, int *infected, int *err, cl_error_t *ret_code);
char **onas_get_opt_list(const char *fname, int *num_entries, cl_error_t *err);
void free_opt_list(char** opt_list, int entries);

#endif