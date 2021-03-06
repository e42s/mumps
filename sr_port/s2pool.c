/****************************************************************
 *								*
 *	Copyright 2001 Sanchez Computer Associates, Inc.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"

#include "stringpool.h"

void
s2pool(mstr *a)
{
	GBLREF spdesc stringpool;
	int al;

	if ((al = a->len) == 0)
		return;
	assert(stringpool.free >= stringpool.base);
	assert(stringpool.free <= stringpool.top);
	if (al > stringpool.top - stringpool.free)
		stp_gcol(al);
	memcpy(stringpool.free,a->addr,al);
	a->addr = (char *)stringpool.free;
	stringpool.free += al;
	assert(stringpool.free >= stringpool.base);
	assert(stringpool.free <= stringpool.top);
}
