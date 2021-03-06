/****************************************************************
 *								*
 *	Copyright 2002, 2008 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_logicals.h"
#include "io.h"
#include "iosp.h"
#include "trans_log_name.h"
#include "startup.h"
#include "stringpool.h"
#include "zdate_form_init.h"
#include "gtm_stdlib.h"

GBLREF spdesc	stringpool;
GBLREF int4	zdate_form;

void zdate_form_init(struct startup_vector *svec)
{
	int4		status;
	mstr		val, tn;
	char		buf[MAX_TRANS_NAME_LEN];

	error_def(ERR_LOGTOOLONG);
	error_def(ERR_TRNLOGFAIL);

	val.addr = ZDATE_FORM;
	val.len = STR_LIT_LEN(ZDATE_FORM);
	if (SS_NORMAL == (status = TRANS_LOG_NAME(&val, &tn, buf, sizeof(buf), dont_sendmsg_on_log2long)))
	{
		assert(tn.len < sizeof(buf));
		buf[tn.len] = '\0';
		zdate_form = (int4)(STRTOL(buf, NULL, 10));
	} else if (SS_NOLOGNAM == status)
		zdate_form = svec->zdate_form;
#	ifdef UNIX
	else if (SS_LOG2LONG == status)
		rts_error(VARLSTCNT(5) ERR_LOGTOOLONG, 3, val.len, val.addr, sizeof(buf) - 1);
#	endif
	else
		rts_error(VARLSTCNT(5) ERR_TRNLOGFAIL, 2, LEN_AND_LIT(ZDATE_FORM), status);
}
