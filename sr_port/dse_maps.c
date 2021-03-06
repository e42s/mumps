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
#include "gtm_time.h"

#include "gdsroot.h"
#include "gdskill.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "gdsblk.h"
#include "gdsbml.h"
#include "min_max.h"		/* needed for gdsblkops.h */
#include "gdsblkops.h"
#include "gdscc.h"
#include "filestruct.h"
#include "jnl.h"
#include "buddy_list.h"		/* needed for tp.h */
#include "hashtab_int4.h"	/* needed for tp.h */
#include "tp.h"
#include "cli.h"
#include "util.h"
#include "mupipbckup.h"
#include "send_msg.h"
#include "dse.h"

/* Include prototypes */
#include "t_qread.h"
#include "dse_is_blk_free.h"
#include "bit_set.h"
#include "bit_clear.h"
#include "t_write.h"
#include "gvcst_blk_build.h"
#include "longset.h"		/* needed for cws_insert.h */
#include "cws_insert.h"
#include "jnl_write_aimg_rec.h"
#include "mm_update.h"
#include "bg_update.h"
#include "wcs_timer_start.h"
#include "process_deferred_stale.h"

#define MAX_UTIL_LEN 80

GBLREF char             *update_array, *update_array_ptr;
GBLREF uint4		update_array_size;
GBLREF sgmnt_addrs      *cs_addrs;
GBLREF sgmnt_data_ptr_t cs_data;
GBLREF block_id         patch_curr_blk;
GBLREF gd_region        *gv_cur_region;
GBLREF short            crash_count;
GBLREF cw_set_element   cw_set[];
GBLREF unsigned char    cw_set_depth;
GBLREF cache_rec	*cr_array[((MAX_BT_DEPTH * 2) - 1) * 2];	/* Maximum number of blocks that can be in transaction */
GBLREF unsigned int	cr_array_index;
GBLREF boolean_t	block_saved;
GBLREF boolean_t        unhandled_stale_timer_pop;
GBLREF unsigned char    *non_tp_jfb_buff_ptr;
GBLREF jnl_gbls_t	jgbl;
GBLREF uint4		process_id;

void dse_maps(void)
{
	block_id		blk, bml_blk;
	blk_segment		*bs1, *bs_ptr;
	cw_set_element		*cse;
	int4			blk_seg_cnt, blk_size;		/* needed for BLK_INIT, BLK_SEG and BLK_FINI macros */
	sgm_info		*dummysi = NULL;
	sm_uc_ptr_t		bp;
	char			util_buff[MAX_UTIL_LEN];
	int4			bml_size, bml_list_size, blk_index, bml_index;
	int4			total_blks, blks_in_bitmap;
	int4			bplmap, dummy_int;
	unsigned char		*bml_list;
	cache_rec_ptr_t		cr, dummy_cr;
	bt_rec_ptr_t		btr;
	int			util_len;
	uchar_ptr_t		blk_ptr;
	bool			was_crit;
	uint4			jnl_status;
	srch_blk_status		blkhist;
	jnl_private_control	*jpc;
	jnl_buffer_ptr_t	jbp;

	error_def(ERR_DSEBLKRDFAIL);
	error_def(ERR_DBRDONLY);

	if (CLI_PRESENT == cli_present("BUSY") || CLI_PRESENT == cli_present("FREE") ||
		CLI_PRESENT == cli_present("MASTER") || CLI_PRESENT == cli_present("RESTORE_ALL"))
	{
	if (gv_cur_region->read_only)
		rts_error(VARLSTCNT(4) ERR_DBRDONLY, 2, DB_LEN_STR(gv_cur_region));
	}
	CHECK_AND_RESET_UPDATE_ARRAY;	/* reset update_array_ptr to update_array */
	assert(&FILE_INFO(gv_cur_region)->s_addrs == cs_addrs);
	was_crit = cs_addrs->now_crit;
	if (cs_addrs->critical)
		crash_count = cs_addrs->critical->crashcnt;
	bplmap = cs_addrs->hdr->bplmap;
	if (CLI_PRESENT == cli_present("BLOCK"))
	{
		if (!cli_get_hex("BLOCK", (uint4 *)&blk))
			return;
		if (blk < 0 || blk >= cs_addrs->ti->total_blks)
		{
			util_out_print("Error: invalid block number.", TRUE);
			return;
		}
		patch_curr_blk = blk;
	}
	else
		blk = patch_curr_blk;
	if (CLI_PRESENT == cli_present("FREE"))
	{
		if (0 == bplmap)
		{
			util_out_print("Cannot perform map updates:  bplmap field of file header is zero.", TRUE);
			return;
		}
		if (blk / bplmap * bplmap == blk)
		{
			util_out_print("Cannot perform action on a map block.", TRUE);
			return;
		}
		bml_blk = blk / bplmap * bplmap;
		bm_setmap(bml_blk, blk, FALSE);
		return;
	}
	if (CLI_PRESENT == cli_present("BUSY"))
	{
		if (0 == bplmap)
		{
			util_out_print("Cannot perform map updates:  bplmap field of file header is zero.", TRUE);
			return;
		}
		if (blk / bplmap * bplmap == blk)
		{
			util_out_print("Cannot perform action on a map block.", TRUE);
			return;
		}
		bml_blk = blk / bplmap * bplmap;
		bm_setmap(bml_blk, blk, TRUE);
		return;
	}
	blk_size = cs_addrs->hdr->blk_size;
	if (CLI_PRESENT == cli_present("MASTER"))
	{
		if (0 == bplmap)
		{
			util_out_print("Cannot perform maps updates:  bplmap field of file header is zero.", TRUE);
			return;
		}
		if (!was_crit)
			grab_crit(gv_cur_region);
		bml_blk = blk / bplmap * bplmap;
		if (dba_mm == cs_addrs->hdr->acc_meth)
			bp = (sm_uc_ptr_t)cs_addrs->acc_meth.mm.base_addr + (off_t)bml_blk * blk_size;
		else
		{
			assert(dba_bg == cs_addrs->hdr->acc_meth);
			if (!(bp = t_qread(bml_blk, &dummy_int, &dummy_cr)))
				rts_error(VARLSTCNT(1) ERR_DSEBLKRDFAIL);
		}
		if ((cs_addrs->ti->total_blks / bplmap) * bplmap == bml_blk)
			total_blks = (cs_addrs->ti->total_blks - bml_blk);
		else
			total_blks = bplmap;
		if (NO_FREE_SPACE == bml_find_free(0, bp + sizeof(blk_hdr), total_blks))
			bit_clear(bml_blk / bplmap, cs_addrs->bmm);
		else
			bit_set(bml_blk / bplmap, cs_addrs->bmm);
		if (bml_blk > cs_addrs->nl->highest_lbm_blk_changed)
			cs_addrs->nl->highest_lbm_blk_changed = bml_blk;
		if (!was_crit)
			rel_crit(gv_cur_region);
		return;
	}
	if (CLI_PRESENT == cli_present("RESTORE_ALL"))
	{
		if (0 == bplmap)
		{
			util_out_print("Cannot perform maps updates:  bplmap field of file header is zero.", TRUE);
			return;
		}
		total_blks = cs_addrs->ti->total_blks;
		assert(ROUND_DOWN2(blk_size, 2 * sizeof(int4)) == blk_size);
		bml_size = BM_SIZE(bplmap);
		bml_list_size = (total_blks + bplmap - 1) / bplmap * bml_size;
		bml_list = (unsigned char *)malloc(bml_list_size);
		for (blk_index = 0, bml_index = 0;  blk_index < total_blks; blk_index += bplmap, bml_index++)
			bml_newmap((blk_hdr_ptr_t)(bml_list + bml_index * bml_size), bml_size, cs_addrs->ti->curr_tn);
		if (!was_crit)
			grab_crit(gv_cur_region);
		blk = get_dir_root();
		assert(blk < bplmap);
		cs_addrs->ti->free_blocks = total_blks - DIVIDE_ROUND_UP(total_blks, bplmap);
		bml_busy(blk, bml_list + sizeof(blk_hdr));
		cs_addrs->ti->free_blocks =  cs_addrs->ti->free_blocks - 1;
		dse_m_rest(blk, bml_list, bml_size, &cs_addrs->ti->free_blocks, TRUE);
		for (blk_index = 0, bml_index = 0;  blk_index < total_blks; blk_index += bplmap, bml_index++)
		{
			CHECK_TN(cs_addrs, cs_data, cs_data->trans_hist.curr_tn);	/* can issue rts_error TNTOOLARGE */
			CWS_RESET;
			CHECK_AND_RESET_UPDATE_ARRAY;	/* reset update_array_ptr to update_array */
			assert(cs_addrs->ti->early_tn == cs_addrs->ti->curr_tn);
			cs_addrs->ti->early_tn++;
			blk_ptr = bml_list + bml_index * bml_size;
			blkhist.blk_num = blk_index;
			if (!(blkhist.buffaddr = t_qread(blkhist.blk_num, &blkhist.cycle, &blkhist.cr)))
				rts_error(VARLSTCNT(1) ERR_DSEBLKRDFAIL);
			BLK_INIT(bs_ptr, bs1);
			BLK_SEG(bs_ptr, blk_ptr + sizeof(blk_hdr), bml_size - sizeof(blk_hdr));
			BLK_FINI(bs_ptr, bs1);
			t_write(&blkhist, (unsigned char *)bs1, 0, 0, LCL_MAP_LEVL, TRUE, FALSE, GDS_WRITE_KILLTN);
			cr_array_index = 0;
			block_saved = FALSE;
			if (JNL_ENABLED(cs_data))
			{
				SET_GBL_JREC_TIME;	/* needed for jnl_ensure_open, jnl_put_jrt_pini and jnl_write_aimg_rec */
				jpc = cs_addrs->jnl;
				jbp = jpc->jnl_buff;
				/* Before writing to jnlfile, adjust jgbl.gbl_jrec_time if needed to maintain time order of jnl
				 * records. This needs to be done BEFORE the jnl_ensure_open as that could write journal records
				 * (if it decides to switch to a new journal file)
				 */
				ADJUST_GBL_JREC_TIME(jgbl, jbp);
				jnl_status = jnl_ensure_open();
				if (0 == jnl_status)
				{
					cse = (cw_set_element *)(&cw_set[0]);
					cse->new_buff = non_tp_jfb_buff_ptr;
					gvcst_blk_build(cse, (uchar_ptr_t)cse->new_buff, cs_addrs->ti->curr_tn);
					cse->done = TRUE;
					if (0 == jpc->pini_addr)
						jnl_put_jrt_pini(cs_addrs);
					jnl_write_aimg_rec(cs_addrs, cse);
				} else
					rts_error(VARLSTCNT(6) jnl_status, 4, JNL_LEN_STR(cs_data), DB_LEN_STR(gv_cur_region));
			}
			if (dba_bg == cs_addrs->hdr->acc_meth)
				bg_update(cw_set, cs_addrs->ti->curr_tn, cs_addrs->ti->curr_tn, dummysi);
			else
				mm_update(cw_set, cs_addrs->ti->curr_tn, cs_addrs->ti->curr_tn, dummysi);
			INCREMENT_CURR_TN(cs_data);
			/* the following code is analogous to that in t_end and should be maintained in a similar fashion */
			UNPIN_CR_ARRAY_ON_COMMIT(cr_array, cr_array_index);
			assert(!cr_array_index);
			if (block_saved)
				backup_buffer_flush(gv_cur_region);
			wcs_timer_start(gv_cur_region, TRUE);
			cw_set_depth = 0;	/* signal end of active transaction to secshr_db_clnup/t_commit_clnup */
		}
		/* Fill in master map */
		for (blk_index = 0, bml_index = 0;  blk_index < total_blks; blk_index += bplmap, bml_index++)
		{
			blks_in_bitmap = (blk_index + bplmap <= total_blks) ? bplmap : total_blks - blk_index;
			assert(1 < blks_in_bitmap);	/* the last valid block in the database should never be a bitmap block */
			if (NO_FREE_SPACE != bml_find_free(0, (bml_list + bml_index * bml_size) + sizeof(blk_hdr), blks_in_bitmap))
				bit_set(blk_index / bplmap, cs_addrs->bmm);
			else
				bit_clear(blk_index / bplmap, cs_addrs->bmm);
			if (blk_index > cs_addrs->nl->highest_lbm_blk_changed)
				cs_addrs->nl->highest_lbm_blk_changed = blk_index;
		}
		if (!was_crit)
			rel_crit(gv_cur_region);
		if (unhandled_stale_timer_pop)
			process_deferred_stale();
		free(bml_list);
		cs_addrs->hdr->kill_in_prog = cs_addrs->hdr->abandoned_kills = 0;
		return;
	}
	MEMCPY_LIT(util_buff, "!/Block ");
	util_len = sizeof("!/Block ") - 1;
	util_len += i2hex_nofill(blk, (uchar_ptr_t)&util_buff[util_len], 8);
	memcpy(&util_buff[util_len], " is marked !AD in its local bit map.!/",
		sizeof(" is marked !AD in its local bit map.!/") - 1);
	util_len += sizeof(" is marked !AD in its local bit map.!/") - 1;
	util_buff[util_len] = 0;
	if (!was_crit)
		grab_crit(gv_cur_region);
	util_out_print(util_buff, TRUE, 4, dse_is_blk_free(blk, &dummy_int, &dummy_cr) ? "free" : "busy");
	if (!was_crit)
		rel_crit(gv_cur_region);
	return;
}
