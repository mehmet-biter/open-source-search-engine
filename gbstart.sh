#!/usr/bin/env bash

working_dir=$(dirname $0)

host_id=
if [ $# -ge 1 ]; then
	host_id=$1
fi

function get_cpu_affinity() {
	if [ -z ${host_id} ]; then
		return
	fi

	# do it the simple way until we decide on a strategy
	if [ ! -f taskset.conf ]; then
		return
	fi

	sed -n $(expr ${host_id} % 14 + 1)p taskset.conf
}

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

	# leftover from previous run
	rm -f cleanexit

	GB_PRE=
	cpu_affinity=$(get_cpu_affinity)
	if [ ! -z ${cpu_affinity} ]; then
		GB_PRE="taskset -c ${cpu_affinity}"
	fi

	${GB_PRE} ./gb -l $ADDARGS

	EXITSTATUS=$?

	# if gb does exit(0) then stop
	# also stop if ./cleanexit is there because exit(0) does not always work for some strange reasons
	if [ $EXITSTATUS = 0 ] || [ -f "cleanexit" ]; then
		break
	fi

	ADDARGS='-r'$INC
	INC=$((INC+1))
done > /dev/null 2>&1 &

