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

#include <stddef.h> /* for offsetof() macro */
#include "gtm_ctype.h"

#include "mlkdef.h"
#include "gtm_string.h"
#include "subscript.h"
#include "gdsroot.h"		/* for filestruct.h */
#include "gdsbt.h"		/* for gdsfhead.h */
#include "gtm_facility.h"	/* for fileinfo.h */
#include "fileinfo.h"		/* for gdsfhead.h */
#include "gdsfhead.h"		/* for filestruct.h */
#include "filestruct.h"		/* for jnl.h */
#include "jnl.h"
#include "repl_dbg.h"
#include "copy.h"
#include "zshow.h"
#include "mvalconv.h"
#include "str2gvkey.h"

GBLREF	char		*ext_stop;
GBLREF	gv_key		*gv_currkey;
LITREF	boolean_t	jrt_fixed_size[JRT_RECTYPES];
LITREF	int		jrt_update[JRT_RECTYPES];
static	boolean_t	in_tp;
static	int4		num_records;

/* callers please set up the proper condition-handlers */
/* expects a null-terminated ext_buff. does the equivalent but inverse of jnl2ext */

char	*ext2jnlcvt(char *ext_buff, int4 ext_len, jnl_record *rec)
{
	char		*ext_next;
	jnl_record	*temp_rec;

	temp_rec = rec;
	for ( ; (NULL != (ext_next = strchr(ext_buff, '\n'))); )
	{
		*ext_next++ = '\0';
		rec = (jnl_record *)ext2jnl(ext_buff, rec);
		assert(0 == (INTPTR_T)rec % JNL_REC_START_BNDRY);
		if (ext_stop == ext_buff)
			break;
		ext_buff = ext_next;
	}

	assert(rec != temp_rec);
	ext_stop = ext_buff;
	return (char *)rec;
}


/* expects a single null-terminated ptr (equivalent to one line in the extract-file) */

char	*ext2jnl(char *ptr, jnl_record *rec)
{
	unsigned char	*pool_save, ch, chtmp;
	int		keylength, keystate, len, i, reclen, temp_reclen;
	bool		keepgoing;
	mstr		src, des;
	jnl_record	*temp_rec;
	muextract_type	exttype;
	enum jnl_record_type	rectype;
	jrec_suffix	*suffix;

	ext_stop = ptr + strlen(ptr) + 1;
	temp_rec = rec;

	exttype = (muextract_type)MUEXTRACT_TYPE(ptr);
	assert((exttype >= 0) && (exttype < MUEXT_MAX_TYPES));

	switch(exttype)
	{
	case MUEXT_SET:
		if (in_tp)
		{
			if (0 == num_records)
			{
				num_records++;
				rec->prefix.jrec_type = JRT_TSET;
			}
			else
				rec->prefix.jrec_type = JRT_USET;
		} else
			rec->prefix.jrec_type = JRT_SET;
		break;

	case MUEXT_KILL:
		if (in_tp)
		{
			if (0 == num_records)
			{
				num_records++;
				rec->prefix.jrec_type = JRT_TKILL;
			}
			else
				rec->prefix.jrec_type = JRT_UKILL;
		} else
			rec->prefix.jrec_type = JRT_KILL;
		break;

	case MUEXT_ZKILL:
		if (in_tp)
		{
			if (0 == num_records)
			{
				num_records++;
				rec->prefix.jrec_type = JRT_TZKILL;
			}
			else
				rec->prefix.jrec_type = JRT_UZKILL;
		} else
			rec->prefix.jrec_type = JRT_ZKILL;
		break;

	case MUEXT_TSTART:
		in_tp = TRUE;
		num_records = 0;
		return (char *)rec;
		break;

	case MUEXT_TCOMMIT:
		rec->prefix.jrec_type = JRT_TCOM;
		in_tp = FALSE;
		break;

	case MUEXT_PINI:
	case MUEXT_PFIN:
	case MUEXT_EOF:
	case MUEXT_ZTSTART:
	case MUEXT_ZTCOMMIT:
		assert(FALSE);
		ext_stop = ptr;
		return (char *)rec;
		break;

	case MUEXT_NULL:
		rec->prefix.jrec_type = JRT_NULL;
		break;

	default:
		assert(FALSE);
		ext_stop = ptr;
		return (char *)rec;
		break;
	}
	rectype = (enum jnl_record_type)rec->prefix.jrec_type;
	ptr = strtok(ptr, "\\");		/* get the rec-type field */
	assert(NULL != ptr);
	ptr = strtok(NULL, "\\");		/* get the time field */
	assert(NULL != ptr);
	ptr = strtok(NULL, "\\");		/* get the tn field */
	assert(NULL != ptr);
	rec->prefix.tn = asc2i((uchar_ptr_t)ptr, STRLEN(ptr));
	ptr = strtok(NULL, "\\");		/* get the pid field */
	assert(NULL != ptr);
	ptr = strtok(NULL, "\\");		/* get the client pid field */
	assert(NULL != ptr);
	ptr = strtok(NULL, "\\");		/* get the token or jnl_seqno */
	assert(NULL != ptr);
	rec->jrec_null.jnl_seqno = asc2l((uchar_ptr_t)ptr,STRLEN(ptr));

	if (MUEXT_NULL == exttype)
	{
		rec->jrec_null.prefix.forwptr =  rec->jrec_null.suffix.backptr = NULL_RECLEN;
		rec->jrec_null.suffix.suffix_code = JNL_REC_SUFFIX_CODE;
		return ((char_ptr_t)rec) + NULL_RECLEN;
	}
	else if (MUEXT_TCOMMIT == exttype)
	{
		ptr = strtok(NULL, "\\");		/* get the participants */
		ptr = strtok(NULL, "\\");		/* get the jnl_tid */
		rec->jrec_tcom.jnl_tid[0] = 0;
		if (NULL != ptr)
			strcpy(rec->jrec_tcom.jnl_tid, ptr);
		num_records = 0;
		rec->jrec_tcom.prefix.forwptr =  rec->jrec_tcom.suffix.backptr = TCOM_RECLEN;
		rec->jrec_tcom.suffix.suffix_code = JNL_REC_SUFFIX_CODE;
		return ((char_ptr_t)rec) + TCOM_RECLEN;
	}
	ptr += (strlen(ptr) + 1); /* get the key-value and data also; can't use strtok since there might be '\\' in the subscript */
	assert(IS_SET_KILL_ZKILL(rectype));
	assert(NULL != ptr);

	len = STRLEN(ptr);
	keylength = zwrkeylength(ptr, len); /* determine length of key */

	REPL_DPRINT2("ext2jnl source:KEY=DATA:%s\n", ptr);
	assert(keylength <= len);
	str2gvkey_nogvfunc(ptr, keylength, gv_currkey);
	rec->jrec_kill.mumps_node.length = gv_currkey->end;
	memcpy(rec->jrec_kill.mumps_node.text, gv_currkey->base, gv_currkey->end);
	temp_reclen = FIXED_UPD_RECLEN + rec->jrec_kill.mumps_node.length + SIZEOF(jnl_str_len_t);
	if (IS_KILL_ZKILL(rectype))
	{
		temp_reclen += JREC_SUFFIX_SIZE;
		reclen = ROUND_UP2(temp_reclen, JNL_REC_START_BNDRY);
		memset((char_ptr_t)rec + temp_reclen - JREC_SUFFIX_SIZE, 0, reclen - temp_reclen);
		suffix = (jrec_suffix *)((char_ptr_t)rec + reclen - JREC_SUFFIX_SIZE);
		rec->prefix.forwptr = suffix->backptr = reclen;
		suffix->suffix_code = JNL_REC_SUFFIX_CODE;
		return (char_ptr_t)rec + reclen;
	}
	/* we have to get the data value now */
	src.len = len - keylength - 1;
	src.addr = ptr + (keylength + 1);
	des.len = 0;
	des.addr = (char_ptr_t)rec + temp_reclen + sizeof(jnl_str_len_t);
	REPL_DPRINT3("ext2jnl JNL Format (before zwr2format): src : Len %d :: DATA:%s\n", src.len, src.addr);
	REPL_DPRINT3("ext2jnl JNL Format (before zwr2format): des : Len %d :: DATA:%s\n", des.len, des.addr);
	if (!zwr2format(&src, &des))
	{
		assert(FALSE);
		return (char_ptr_t)rec;
	}
	REPL_DPRINT3("ext2jnl JNL Format : src : Len %d :: DATA:%s\n", src.len, src.addr);
	REPL_DPRINT3("ext2jnl JNL Format : des : Len %d :: DATA:%s\n", des.len, des.addr);
	PUT_MSTR_LEN((char_ptr_t)rec + temp_reclen, des.len);
	temp_reclen += sizeof(jnl_str_len_t) + des.len + JREC_SUFFIX_SIZE;
	reclen = ROUND_UP2(temp_reclen, JNL_REC_START_BNDRY);
	memset((char_ptr_t)rec + temp_reclen - JREC_SUFFIX_SIZE, 0, reclen - temp_reclen);
	suffix = (jrec_suffix *)((char_ptr_t)rec + reclen - JREC_SUFFIX_SIZE);
	rec->prefix.forwptr = suffix->backptr = reclen;
	suffix->suffix_code = JNL_REC_SUFFIX_CODE;
	return (char_ptr_t)rec + reclen;
}
