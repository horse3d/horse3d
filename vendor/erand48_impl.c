
/*
 * This code is based on NetBSD, with originally this license:
 *
 *
 * Copyright (c) 1993 Martin Birgmeier
 * All rights reserved.
 *
 * You may redistribute unmodified or modified versions of this source
 * code provided that the above copyright notice and this and the
 * following conditions are retained.
 *
 * This software is provided ``as is'', and comes with no warranties
 * of any kind. I shall in no event be liable for anything that happens
 * to anyone/anything when using this software.
 *
 *
 * Any changes done here for Horse64 beyond the version with
 * originally above copyright are public domain/CC0, but mostly
 * cosmetic anyway.
 */

#include <math.h>
#include <stdint.h>

double netbsd_erand48(uint16_t xseed[3]) {
	return ldexp((double) xseed[0], -48) +
	       ldexp((double) xseed[1], -32) +
	       ldexp((double) xseed[2], -16);
}
