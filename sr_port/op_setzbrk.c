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

#include "gtm_string.h"

#include "cache.h"
#include "hashtab_objcode.h"
#include "rtnhdr.h"
#include "zbreak.h"
#include "stack_frame.h"
#include "xfer_enum.h"
#include "indir_enum.h"
#include "cachectl.h"
#include "op.h"
#include "fix_pages.h"
#include "io.h"
#include "inst_flush.h"
#include "private_code_copy.h"
#include "iosp.h"
#include "gtm_text_alloc.h"

#ifdef __ia64
#include "ia64.h"
#endif /*__ia64__*/

GBLREF z_records		zbrk_recs;
GBLREF mident_fixed		zlink_mname;
GBLREF stack_frame		frame_pointer;
GBLREF unsigned short		proc_act_type;
GBLREF hash_table_objcode	cache_table;

void	op_setzbrk(mval *rtn, mval *lab, int offset, mval *act, int cnt)
	/* act == action associated with ZBREAK */
	/* cnt == perform break after this many passes */
{
	char		*cp, zbloc_buff[MAX_ENTRYREF_LEN], *zbloc_end;
	mident		*lab_name, *dummy;
	mident		rname, lname;
	mstr		*obj;
	rhdtyp		*routine;
	zb_code		*addr, tmp_xf_code;
	int4		*line_offset_addr, *next_line_offset_addr;
	ssize_t		addr_off;
	zbrk_struct	*z_ptr;
	cache_entry	*csp;
	uint4		status;
	icode_str	indir_src;
	boolean_t	deleted;
	error_def(ERR_ZLINKFILE);
	error_def(ERR_ZLMODULE);
	error_def(ERR_NOZBRK);
	error_def(ERR_COMMENT);
	error_def(ERR_NOPLACE);
	error_def(ERR_ZBREAKFAIL);
	error_def(ERR_MEMORY);
	error_def(ERR_VMSMEMORY);

	MV_FORCE_STR(rtn);
	MV_FORCE_STR(lab);
	MV_FORCE_STR(act);
	if (NULL == zbrk_recs.beg)
	{
		assert(NULL == zbrk_recs.free);
		assert(NULL == zbrk_recs.end);
		zr_init(&zbrk_recs, INIT_NUM_ZBREAKS);
	}
	if (CANCEL_ALL == cnt)
		zr_remove(NULL);
	else
	{
		flush_pio();
		if (NULL == (routine = find_rtn_hdr(&rtn->str)))
		{
			op_zlink(rtn, NULL);
			routine = find_rtn_hdr(&rtn->str);
			if (NULL == routine)
				rts_error(VARLSTCNT(8) ERR_ZLINKFILE, 2, rtn->str.len, rtn->str.addr,
						ERR_ZLMODULE, 2, mid_len(&zlink_mname), &zlink_mname.c[0]);
		}
		lab_name = NULL;
		if (NULL == (line_offset_addr = find_line_addr(routine, &lab->str, offset, &lab_name)))
			dec_err(VARLSTCNT(1) ERR_NOPLACE);
		else if (CANCEL_ONE == cnt)		/* cancel zbreak */
		{
			addr = (zb_code *)LINE_NUMBER_ADDR(CURRENT_RHEAD_ADR(routine), line_offset_addr);
			addr = find_line_call(addr);
			if (NULL != (z_ptr = zr_find(&zbrk_recs, addr)))
				zr_put_free(&zbrk_recs, z_ptr);
			else
				dec_err(VARLSTCNT(1) ERR_NOZBRK);
		} else if (0 <= cnt)		/* set zbreak */
		{
#ifdef ZB_AT_COMMENT_INFO
			dummy = NULL;
			next_line_offset_addr = find_line_addr(routine, &lab->str, offset + 1, &dummy);
			if (NULL != next_line_offset_addr && *next_line_offset_addr == *line_offset_addr)
			{ /* we don't recognize the case of last line comment 'coz that line generates LINESTART, RET code */
				dec_err(VARLSTCNT(1) ERR_COMMENT);
				assert(lab_name == dummy);
			}
#endif
			op_commarg(act, indir_linetail); /* This puts entry in stack and also increments refcnt field */
			indir_src.str = act->str;
			indir_src.code = indir_linetail;
			obj = cache_get(&indir_src);
			assert(NULL != obj);
			csp = ((ihdtyp *)(obj->addr))->indce;	/* Cache entry for this object code */
			csp->zb_refcnt++;			/* This will keep it around */
			op_unwind();				/* This removes entry from stack and decrements refcnt field */
			addr = (zb_code *)LINE_NUMBER_ADDR(CURRENT_RHEAD_ADR(routine), line_offset_addr);
			/* On HPPA (& other platforms) the addr returned is the address of the field in the instruction which is
			   the offset of xfer table. But on IA64, as this field is set in the ADDS instruction as 14 bit immed
			   value and this is written into 3 bitwise fields, we need to return the address of the whole instruction
			   itself (in our case bundle as we are using a bundle for every instruction in the generated code.
			*/
			addr = find_line_call(addr);
			if (NULL == (z_ptr = zr_find(&zbrk_recs, addr)))
			{
#ifdef USHBIN_SUPPORTED
				if (NULL != routine->shlib_handle && NULL == routine->shared_ptext_adr)
				{ /* setting a breakpoint in a shared routine, need to make a private copy */
					addr_off = (unsigned char *)addr - routine->ptext_adr;
					if (SS_NORMAL == (status = cre_private_code_copy(routine)))
						addr = (zb_code *)(routine->ptext_adr + addr_off);
					else
					{
						assert(UNIX_ONLY(ERR_MEMORY) VMS_ONLY(ERR_VMSMEMORY) == status);
						/* convert to label+offset^routine to be presented to the user */
						rname.len = rtn->str.len;
						rname.addr = rtn->str.addr;
						lname.len = lab->str.len;
						lname.addr = lab->str.addr;
						zbloc_end = rtnlaboff2entryref(zbloc_buff, &rname, &lname, offset);
						rts_error(VARLSTCNT(4) ERR_ZBREAKFAIL, 2, zbloc_end - zbloc_buff,
								zbloc_buff);
					}
				}
#endif
				z_ptr = zr_get_free(&zbrk_recs, addr);
				NON_USHBIN_ONLY(fix_pages((unsigned char *)addr, (unsigned char *)addr));
#ifndef __ia64
				z_ptr->m_opcode = *addr; /* save for later restore while cancelling breakpoint */
				tmp_xf_code = (z_ptr->m_opcode & ZB_CODE_MASK) >> ZB_CODE_SHIFT;
#else
				ia64_fmt_A4 inst;
				/* Revist when instruction bundling implemented. */
				EXTRACT_INST(addr, inst, 3);
				z_ptr->m_opcode = ASSEMBLE_14(inst);
				tmp_xf_code = (z_ptr->m_opcode & ZB_CODE_MASK) >> ZB_CODE_SHIFT;
#endif /* __ia64*/
				if (xf_linefetch * sizeof(UINTPTR_T) == tmp_xf_code)
#ifndef __ia64
					*addr = (*addr & (zb_code)(~ZB_CODE_MASK)) |
						((xf_zbfetch * sizeof(UINTPTR_T)) << ZB_CODE_SHIFT);
#else
				{
					zb_code imm14 = xf_zbfetch * sizeof(UINTPTR_T);
					inst.format.imm7b=imm14;
					inst.format.imm6d=imm14 >> 7;
					inst.format.sb=imm14 >> 13;
					/* Revist when instruction bundling implemented. */
					UPDATE_INST(addr, inst, 3);
				}
#endif
				else if (xf_linestart * sizeof(UINTPTR_T) == tmp_xf_code)
#ifndef __ia64
					*addr = (*addr & (zb_code)(~ZB_CODE_MASK)) |
						((xf_zbstart * sizeof(UINTPTR_T)) << ZB_CODE_SHIFT);
#else
				{
					zb_code imm14 = xf_zbstart * sizeof(UINTPTR_T);
					inst.format.imm7b=imm14;
					inst.format.imm6d=imm14 >> 7;
					inst.format.sb=imm14 >> 13;
					/* Revist when instruction bundling implemented. */
					UPDATE_INST(addr, inst, 3);
				}
#endif
				else if (((xf_zbstart * sizeof(UINTPTR_T)) != tmp_xf_code)
					&& ((xf_zbfetch * sizeof(UINTPTR_T)) != tmp_xf_code))
					GTMASSERT;
				z_ptr->rtn = &(CURRENT_RHEAD_ADR(routine))->routine_name;
				assert(lab_name != NULL);
				z_ptr->lab = lab_name;
				z_ptr->offset = offset;
				z_ptr->mpc = (zb_code *)((unsigned char *)addr - SIZEOF_LA);
#ifndef __ia64
				inst_flush(addr, sizeof(*addr));
#else
				inst_flush(addr, sizeof(ia64_bundle));
#endif
			}
			if (z_ptr->action)
			{	/* A zbreak command was already set for this line */
				/* Note when new action is same as old action, no resultant changes in zb_refcnt */
				assert(z_ptr->action->zb_refcnt > 0);
				z_ptr->action->zb_refcnt--;
				assert(z_ptr->action != csp || z_ptr->action->zb_refcnt > 0);
				if (z_ptr->action != csp && 0 == z_ptr->action->refcnt && 0 == z_ptr->action->zb_refcnt)
				{	/* new action is different one, this is not in stack and no zbreak points to this */
					deleted = delete_hashtab_objcode(&cache_table, &z_ptr->action->src);
					assert(deleted);
					GTM_TEXT_FREE(z_ptr->action);
				}
			}
			z_ptr->action = csp;
			z_ptr->count = cnt;
		} else
			GTMASSERT;
	}
}
