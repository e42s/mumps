/****************************************************************
 *								*
 *	Copyright 2001, 2008 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"

#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "stringpool.h"
#include "op.h"
#include "gvcst_protos.h"	/* for gvcst_order,gvcst_data prototype */
#include "change_reg.h"
#include "gvsub2str.h"
#include "gvcmx.h"
#include "gvusr.h"

GBLREF gv_namehead	*gv_target;
GBLREF gv_key		*gv_currkey;
GBLREF gv_key		*gv_altkey;
GBLREF bool		gv_curr_subsc_null;
GBLREF gd_region	*gv_cur_region;
GBLREF gd_addr		*gd_header;
GBLREF gd_binding	*gd_map;
GBLREF gd_binding	*gd_map_top;
GBLREF sgmnt_addrs	*cs_addrs;
GBLREF spdesc		stringpool;

/* op_zprevious should generally be maintained in parallel */

void op_gvorder (mval *v)
{
	int4			n;
	gd_binding		*map;
	mstr			name;
	enum db_acc_method	acc_meth;
	boolean_t		found;

	/* Modify gv_currkey to reflect the next possible key value in collating order */
	if (!gv_curr_subsc_null || gv_cur_region->std_null_coll)
	{
		*(&gv_currkey->base[0] + gv_currkey->end - 1) = 1;
		*(&gv_currkey->base[0] + gv_currkey->end + 1) = 0;
		gv_currkey->end += 1;
	} else
		*(&gv_currkey->base[0] + gv_currkey->prev) = 01;

	if (gv_currkey->prev)
	{
		acc_meth = gv_cur_region->dyn.addr->acc_meth;
		if (acc_meth == dba_bg || acc_meth == dba_mm)
		{
			if (gv_target->root == 0)	/* global does not exist */
				found = FALSE;
			else
				found = gvcst_order();
		} else if (acc_meth == dba_cm)
			found = gvcmx_order();
		else
		 	found = gvusr_order();

		v->mvtype = MV_STR;
		if (found)
		{
			gv_altkey->prev = gv_currkey->prev;

	 		if (stringpool.top - stringpool.free < MAX_KEY_SZ)
 			{
				if (*(&gv_altkey->base[0] + gv_altkey->prev) != 0xFF)
		 			n = MAX_FORM_NUM_SUBLEN;
				else
				{
					n = gv_altkey->end - gv_altkey->prev;
					assert (n > 0);
				}
		 		if (stringpool.top - stringpool.free < n)
				{
					v->str.len = 0;	/* so stp_gcol ignores otherwise incompletely setup mval */
					stp_gcol(n);
				}
			}
	 		v->str.addr = (char *)stringpool.free;
	 		stringpool.free = gvsub2str (&gv_altkey->base[0] + gv_altkey->prev, stringpool.free, FALSE);
	 		v->str.len = INTCAST((char *)stringpool.free - v->str.addr);
			assert (v->str.addr < (char *)stringpool.top && v->str.addr >= (char *)stringpool.base);
			assert (v->str.addr + v->str.len <= (char *)stringpool.top &&
				v->str.addr + v->str.len >= (char *)stringpool.base);
		} else
			v->str.len = 0;
		/* Reset gv_currkey from next possible key value to what is was at function entry time */
		if (!gv_curr_subsc_null || gv_cur_region->std_null_coll)
		{
			assert(1 == gv_currkey->base[gv_currkey->end - 2]);
			assert(KEY_DELIMITER == gv_currkey->base[gv_currkey->end-1]);
			assert(KEY_DELIMITER == gv_currkey->base[gv_currkey->end]);
			gv_currkey->base[gv_currkey->end - 2] = KEY_DELIMITER;
			gv_currkey->end--;
		} else
			*(&gv_currkey->base[0] + gv_currkey->prev) = 01;
	} else	/* the following section is for $O(^gname) */
	{
		assert (2 < gv_currkey->end);
		assert (gv_currkey->end < (MAX_MIDENT_LEN + 3));	/* until names are not in midents */
		map = gd_map + 1;
		while (map < gd_map_top &&
			(memcmp(gv_currkey->base, map->name,
				gv_currkey->end == (MAX_MIDENT_LEN + 2) ? MAX_MIDENT_LEN : gv_currkey->end - 1) >= 0))
		{
			map++;
		}

		for (; map < gd_map_top; ++map)
		{
			gv_cur_region = map->reg.addr;
			if (!gv_cur_region->open)
				gv_init_reg(gv_cur_region);
			change_reg();
			acc_meth = gv_cur_region->dyn.addr->acc_meth;

			for (; ;)		/* search region, entries in directory tree could be empty */
			{
				if (acc_meth == dba_bg || acc_meth == dba_mm)
				{
					gv_target = cs_addrs->dir_tree;
					found = gvcst_order ();
				} else if (acc_meth == dba_cm)
					found = gvcmx_order ();
				else
				 	found = gvusr_order();
				if (!found)
					break;
				assert (1 < gv_altkey->end);
				assert (gv_altkey->end < (MAX_MIDENT_LEN + 2));	/* until names are not in midents */
				if (memcmp(gv_altkey->base, map->name, gv_altkey->end - 1) > 0)
				{
					found = FALSE;
					break;
				}
				name.addr = (char *)&gv_altkey->base[0];
				name.len = gv_altkey->end - 1;
				if (acc_meth == dba_cm)
					break;
				gv_bind_name(gd_header, &name);
				if (gv_cur_region != map->reg.addr)
				{
					found = FALSE;
					break;
				}
				if ((gv_target->root != 0) && (gvcst_data() != 0))
					break;
				*(&gv_currkey->base[0] + gv_currkey->end - 1) = 1;
				*(&gv_currkey->base[0] + gv_currkey->end + 1) = 0;
				gv_currkey->end += 1;
			}
			if (found)
				break;
			else
			{
				assert(sizeof(map->name) == sizeof(mident_fixed));
				gv_currkey->end = mid_len((mident_fixed *)map->name);
				memcpy(&gv_currkey->base[0], map->name, gv_currkey->end);
				gv_currkey->base[ gv_currkey->end - 1 ] -= 1;
				gv_currkey->base[ gv_currkey->end ] = 0xFF;	/* back off 1 spot from map */
				gv_currkey->base[ gv_currkey->end + 1] = 0;
				gv_currkey->base[ gv_currkey->end + 2] = 0;
				gv_currkey->end += 2;
			}
		}
		/* Reset gv_currkey as we have potentially skipped one or more regions so we no
		 * longer can expect gv_currkey/gv_cur_region/gv_target to match each other.
		 */
		gv_currkey->end = 0;
		gv_currkey->base[0] = 0;
		v->mvtype = MV_STR;
		if (found)
		{
			if (stringpool.free + name.len + 1 > stringpool.top)
			{
				v->str.len = 0; /* so stp_gcol ignores otherwise incompletely setup mval */
				stp_gcol (name.len + 1);
			}
#ifdef mips
			/* the following line works around a tandem compiler bug. */
			v->str.addr = (char *)0;
#endif
			v->str.addr = (char *)stringpool.free;
			*stringpool.free++ = '^';
			memcpy (stringpool.free, name.addr, name.len);
			stringpool.free += name.len;
			v->str.len = name.len + 1;
			assert (v->str.addr < (char *)stringpool.top && v->str.addr >= (char *)stringpool.base);
			assert (v->str.addr + v->str.len <= (char *)stringpool.top &&
				v->str.addr + v->str.len >= (char *)stringpool.base);
		} else
			v->str.len = 0;
	}
	return;
}
