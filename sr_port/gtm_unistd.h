/****************************************************************
 *								*
 *	Copyright 2001, 2006 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* gtm_unistd.h - interlude to <unistd.h> system header file.  */
#ifndef GTM_UNISTDH
#define GTM_UNISTDH

#include <unistd.h>

#define CHDIR		chdir

#define CHOWN		chown

#if defined(VMS)

#define GTM_MAX_DIR_LEN		(PATH_MAX + PATH_MAX) /* DEVICE + DIRECTORY */
#define GTM_VMS_STYLE_CWD	1
#define GTM_UNIX_STYLE_CWD	0

#define GETCWD(buffer, size, getcwd_res)			\
	(getcwd_res = getcwd(buffer, size, GTM_VMS_STYLE_CWD)) /* force VMS style always 'cos many other parts of GT.M always
								* do it the VMS way */
#else /* !VMS => UNIX */

#define GTM_MAX_DIR_LEN		(PATH_MAX + 1) /* DIRECTORY + terminating '\0' */

#define GETCWD(buffer, size, getcwd_res)			\
	(getcwd_res = getcwd(buffer, size))

#endif

#ifndef UNICODE_SUPPORTED
#define GETHOSTNAME(name,namelen,gethostname_res)			\
	(gethostname_res = gethostname(name, namelen))
#else
#include "gtm_utf8.h"
GBLREF	boolean_t	gtm_utf8_mode;
#define GETHOSTNAME(name,namelen,gethostname_res)					\
	(gethostname_res = gethostname(name, namelen),					\
	gtm_utf8_mode ? gtm_utf8_trim_invalid_tail((unsigned char *)name, namelen) : 0,	\
	gethostname_res)
#endif

#define LINK		link

#define UNLINK		unlink

#define TTYNAME		ttyname

#define ACCESS		access

#define EXECL		execl
#define EXECV		execv
#define EXECVE		execve

#define TRUNCATE	truncate

#endif
