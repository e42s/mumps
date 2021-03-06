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

#ifndef LKE_FILEIO_INCLUDED
#define LKE_FILEIO_INCLUDED

bool open_fileio(int *save_stderr);
void close_fileio(int save_stderr);

#endif /* LKE_FILEIO_INCLUDED */
