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

function send_alert() {
	# if slacktee is present, send alert using Slack
	if [ -f "slacktee.sh" ] && [ -f "slacktee.conf" ]; then
		echo -e "`hostname`:`pwd`: $1\n\n" > lastcore.alert.txt
#		ls -tr log*|tail -1|xargs tail -30 >> lastcore.alert.txt

		# if core is there, send first 20 lines of backtrace
		if [ -f "core" ]; then
			gdb --batch --quiet -ex "bt full" -ex "quit" ./gb ./core | grep -v LWP | head -20 >> lastcore.alert.txt
		fi

		cat lastcore.alert.txt | ./slacktee.sh --config ./slacktee.conf
	fi
}

function backup_core() {
	if [ -f "core" ]; then
		# we only keep one copy to avoid filling up the disk if dumping repeatedly..
		cp gb lastcore.gb

		# cp not mv to avoid potentially overwriting logs
		cp `ls -tr log*|tail -1` lastcore.log
		gdb --batch --quiet -ex "thread apply all bt full" -ex "quit" ./gb ./core > lastcore.bt-bak$(date +%Y%m%d-%H%M%S).txt
		mv core lastcore.core
	fi
}

function backup_core_and_alert_if_found() {
	if [ -f "core" ]; then
		file core|grep ./gb
		EXITSTATUS=$?
		if [ $EXITSTATUS = 0 ]; then
			# core is ours
			send_alert "$1"
			backup_core
		else
			mv core lastcore.notgb.core
		fi
	fi
}



# we should use working directory
cd ${working_dir}

# Don't allow startup if fatal_error file exists
if [ -f fatal_error ]; then
    send_alert "FATAL ERROR. Cannot start."
    exit 1
fi

# alert if core exists
backup_core_and_alert_if_found "Core found at startup"


##################################
# verify system converters exist #
##################################

# pdf
which pdftohtml >/dev/null 2>&1 || exit $?


############
# Start gb #
############

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

	# rename if exist
	if [ -f file_state.txt ]; then
		mv file_state.txt file_state-bak$(date +%Y%m%d-%H%M%S).txt
	fi

	# Dump list of files before allowing gb to continue running
	find . -not -path '*/\.*' -not -path '*/__*' -not -path './file_state*' -type f -exec ls -l --full-time {} \; 2>/dev/null |column -t|sort -k 9 > file_state.txt

	${GB_PRE} ./gb -l $ADDARGS

	EXITSTATUS=$?

	# if gb does exit(0) then stop
	# also stop if ./cleanexit is there because exit(0) does not always work for some strange reasons
	if [ $EXITSTATUS = 0 ] || [ -f "cleanexit" ]; then
		break
	fi

	# stop if ./fatal_error is there
	if [ -f "fatal_error" ]; then
		send_alert "FATAL ERROR. Shut down."
		break
	fi

	# not a clean shutdown. Give it a few seconds do finish dumping core
	sleep 5

	# alert if core exists
	backup_core_and_alert_if_found "GB died, core dumped."

	ADDARGS='-r'$INC
	INC=$((INC+1))
done > /dev/null 2>&1 &

