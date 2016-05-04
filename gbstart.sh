#!/usr/bin/env bash

working_dir=$(dirname $0)

# we should use working directory
cd ${working_dir}

# Don't allow startup if fatal_error file exists
if [ -f fatal_error ]; then
    exit 1
fi

##################################
# verify system converters exist #
##################################

# pdf
which pdftohtml >/dev/null 2>&1 || exit $?

############
# Start gb #
############

# Dump list of files before allowing gb to continue running
find . -not -path '*/\.*' -type f -exec ls -l --full-time {} \; 2>/dev/null |column -t|sort -k 9 > file_state_before_run.txt

# set env
ulimit -c unlimited
export MALLOC_CHECK_=0

cp -f gb gb.oldsave

# intialize variable
ADDARGS=''
INC=1 

while true; do
	# in case gb was updated
	mv -f gb.installed gb

	./gb -l $ADDARGS

	EXITSTATUS=$?

	# if gb does exit(0) then stop
	if [ $EXITSTATUS = 0 ]; then
		break
	fi

	# also stop if ./cleanexit is there because exit(0) does not always work for some strange reasons
	if [ -f "cleanexit" ]; then
		# make sure we don't affect next run
		rm -f cleanexit

		break
	fi

	ADDARGS='-r'$INC
	INC=$((INC+1))
done > /dev/null 2>&1 &

