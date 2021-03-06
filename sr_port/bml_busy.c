/****************************************************************
 *								*
 *	Copyright 2001, 2007 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include "gdsroot.h"
#include "gdsbt.h"
#include "gdsblk.h"
#include "gdsbml.h"

/* Include prototypes */
#include "bit_clear.h"

GBLREF	boolean_t		dse_running;

uint4 bml_busy(uint4 setbusy, sm_uc_ptr_t map)
{
	uint4	ret, ret1;

	setbusy *= BML_BITS_PER_BLK;
	ret = bit_clear(setbusy, map);
	ret1 = bit_clear(setbusy + 1, map);
	/* Assert that only a RECYCLED or FREE block gets marked as BUSY (dse is an exception) */
	assert((ret && ret1) || (ret && !ret1) || dse_running);
	return ret;
}
