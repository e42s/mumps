#################################################################
#                                                               #
#       Copyright 2008 Fidelity Information Services, Inc       #
#                                                               #
#       This source code contains the intellectual property     #
#       of its copyright holder(s), and is made available       #
#       under a license.  If you do not know the terms of       #
#       the license, please stop and do not read further.       #
#                                                               #
#################################################################

# Most of the logic in this script is similar to its counter-part on sr_ia64
# Any changes or bugfixes in this files should be updated in its couterpart on sr_ia64

# This script is called from two places. If it is called from comlist.mk,it takes source directories as arguments.
# else it is called from comlist.csh with no arguments.
if ( $#argv != 0 ) then
	set builds=$buildtypes
	set numbuilds=$#builds
	if ( 1 != $numbuilds ) then
		echo "sr_x86_64/gen_xfer_desc.csh-E-2many: only one buildtype at a time allowed"
		exit 2
	endif
	cd $gtm_ver/$buildtypes/obj
	set lib_count=$#argv
	set ref_libs=""

	while ( $lib_count != 0 )
		set ref_libs="$ref_libs $argv[$lib_count]"
		@ lib_count--
	end

	set gtm_src_types = "c m64 s msg"
	set gtm_inc_types = "h max mac si"
	set xfer_dir=`pwd`

	if (-e src) then
		\rm -rf src
	endif

	if (-e inc ) then
		\rm -rf inc
	endif
# Create temporary directories called src and inc
	mkdir src inc

	pushd $gtm_ver

# Following "foreach" logic comes from cms_load.csh
	foreach ref_library ( $ref_libs )
		cd $ref_library
	        foreach dir (src inc)
	                foreach ftype (`set | grep "^gtm_${dir}_t" | sed 's/^gtm_'$dir'_types[  ][      ]*//g'`)
	                        set nfiles = `\ls -1 | grep "\.$ftype"'$' | wc -l | sed 's/^[ ]*//g'`
	                        if ($nfiles != 0) then
#creates the links for all specific  files in src and inc directory.
	                                \ls -1 | grep "\.$ftype"'$' | xargs -i ln -f -s "$PWD/{}" $xfer_dir/${dir}
	                        endif
	                end
	        end
		cd ..
	end

	popd
	setenv gtm_src `pwd`/src
	setenv gtm_inc `pwd`/inc
	setenv gt_cc_option_I "$gt_cc_option_I -I$gtm_inc"
	rm -rf $xfer_dir/xfer_desc.i
else
#	set xfer_desc.i path to $gtm_inc in normal build
	set xfer_dir=$gtm_inc

# If this is a non-developmental version and the current image is "dbg" and xfer_desc.i already exists, do not recreate xfer_desc.i.
# The assumption is that a "pro" build had already created xfer_desc.i so we should not change whatever it had relied upon.
# For development versions, we dont care so we unconditionally recreate this file.

	if (-e $xfer_dir/xfer_desc.i) then
		if ($gtm_verno !~ V9*) then
			echo "GENXFERDESC-I-EXIST : xfer_desc.i already exists for production version $gtm_verno. Not recreating."
			exit 0
		else
			echo "GENXFERDESC-I-EXIST : xfer_desc.i already exists for development version $gtm_verno. Recreating."
			chmod +w $xfer_dir/xfer_desc.i	# in case previous build had reset permissions to be read-only
			rm -f $xfer_dir/xfer_desc.i
		endif
	endif
endif

cd $gtm_src

\rm -f temp_xyz_ia.* >&! /dev/null
cat << TEST >! temp_xyz_ia.c
#include "mdef.h"
#define XFER(a,b) MY_XF,b
#include "xfer.h"
TEST

$gt_cc_compiler $gt_cc_option_I -E temp_xyz_ia.c >! temp_xyz_ia.1
awk -F , '/MY_XF/ {print $2}' temp_xyz_ia.1 >! temp_xyz_ia.2

cat >>  $xfer_dir/xfer_desc.i << EOF
/* Generated by gen_xfer_desc.csh */
#define C 1
#define ASM 2
#define C_VAR_ARGS 3
#define DEFINE_XFER_TABLE_DESC char xfer_table_desc[] = \\
{ \\
EOF

# On X86_64, the calling convention for variable length functions defines that the register $RAX
# (ie the lower 8 bytes of this register actually) will contain the # of floating point arguments passed to that function.
# $RAX is not an argument register normally and so is typically unused (and also is not expected to be preserved across calls).
# since none of the XFER functions actually take double/float arguments, but some are variable length functions,
# the generated code should set the $RAX register to the value '0'.
# Theoritically this register can be set to 0 all the time during a function call.
# But to optimize the number of generated instructions, identify which XFER function is actually a var arg function
# And if the logic to identify it falls thro, blindly assume its a VAR_ARG function.
# Hence the logic below might incorrectly mark a few functions like op_sub, op_fnzascii as C_VAR_ARGS. But that is okay.

foreach name (`cat temp_xyz_ia.2`)
	set name2 = `grep "^$name" *.s`
	if (-r ${name}.s) then
		set ftype = "ASM"
	else if (-r ${name}.c) then
		grep $name $gtm_src/${name}.c | grep "\.\.\." >> /dev/null
		if ( $? == 0 ) then
			set ftype = "C_VAR_ARGS"
		else
			set ftype = "C"
		endif
	else if ("${name2}" != "") then
		set ftype = "ASM"
	else if ("${name2}" == "") then
		set ftype = "C_VAR_ARGS"
	endif
	echo "$ftype, /* $name */ \\"  >> $xfer_dir/xfer_desc.i
	# print the #defines in a temp file to append to $xfer_dir/xfer_desc.i later.
	# This is done to avoid the whole loop once again.
	echo "#define ${name}_FUNCTYPE $ftype" >> temp_xyz_ia.defines
end
echo "0}" >> $xfer_dir/xfer_desc.i
echo " " >> $xfer_dir/xfer_desc.i

# The defines used in resetting xfer_table_desc needs to be generated only for ia64
set mach_type = `uname -m`
if ( "ia64" == "$mach_type") then
	echo "/* Defines used in resetting xfer_table_desc on transfer table changes */" >> $xfer_dir/xfer_desc.i

	# Guess what! Its possible for xfer_table to be intialized by funtions others than
	# then the ones in xfer.h. So append those names explicitly here
	cat >> temp_xyz_ia.3 << EOF
op_fnzreverse
op_zst_st_over
op_zst_fet_over
op_zstzb_fet_over
op_zstzb_st_over
opp_zstepret
opp_zstepretarg
op_zstepfetch
op_zstepstart
op_zstzbfetch
op_zstzbstart
opp_zst_over_ret
opp_zst_over_retarg
op_fetchintrrpt
op_startintrrpt
op_forintrrpt
op_mprofextexfun
op_mprofextcall
op_mprofcalll
op_mprofcallb
op_mprofcallw
op_mprofcallspl
op_mprofcallspb
op_mprofcallspw
op_mprofexfun
op_mprofforlcldow
op_mprofforlcldol
op_mprofforlcldob
op_mprofforloop
op_mproflinefetch
op_mproflinestart
EOF

	foreach name (`cat temp_xyz_ia.3`)
		set name2 = `grep "^$name" *.s`
		if (-r ${name}.s) then
			set ftype = "ASM"
		else if (-r ${name}.c) then
			grep $name $gtm_inc/* | grep "\.\.\." >> /dev/null
			if ( $? == 0 ) then
				set ftype = "C_VAR_ARGS"
			else
				set ftype = "C"
			endif
		else if ("${name2}" != "") then
			set ftype = "ASM"
		else if ("${name2}" == "") then
			set ftype = "C_VAR_ARGS"
		endif
		echo "#define ${name}_FUNCTYPE $ftype" >> temp_xyz_ia.defines
	end
	# Append the defines to the end of $xfer_dir/xfer_desc.i
	cat temp_xyz_ia.defines >> $xfer_dir/xfer_desc.i
	echo
endif

\rm temp_xyz_ia.*

if ($#argv != 0) then
	cd $xfer_dir
	\rm -rf src
	\rm -rf inc
endif

exit 0

