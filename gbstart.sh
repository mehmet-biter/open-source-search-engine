#!/usr/bin/env bash

working_dir=$(dirname $0)

host_id=
if [ $# -ge 1 ]; then
	host_id=$1
fi

if [ -z ${host_id} ]; then
	logfile="log000"
else
	logfile="log"$(printf "%03d" $host_id)
fi

function get_datetime() {
	date -u +%Y-%m-%dT%H:%M:%SZ
}

function get_gb_version() {
	version=$(./gb -v |awk -F: '{for (i=2; i<NF; i++) printf $i ":"; print $NF}' |xargs -i echo -n "{}|")
	echo ${version%?}
}

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

function find_newest_file() {
	ls -t ${1} 2>/dev/null |head -1
}

function send_alert() {
	# if slacktee is present, send alert using Slack
	if [ -f "slacktee.sh" ] && [ -f "slacktee.conf" ]; then
		# Send single-line alert to make sure it is received
		echo -e "`hostname`:`pwd`: $1" | ./slacktee.sh --config slacktee.conf >/dev/null
	else
		# Log alert in logfile
		echo -e "$(date -u "+%Y%m%d-%H%M%S-%3N") $(printf '%04d %06d' $host_id $$) ERR $(basename $0): $1" >> $logfile
	fi
}

function send_core_alert() {
	# if slacktee is present, send alert using Slack
	if [ -f "slacktee.sh" ] && [ -f "slacktee.conf" ]; then
		HOSTNAME=$(hostname)
		if [ -f lastcore.bt.txt ]; then
			head -50 lastcore.bt.txt | ./slacktee.sh --config slacktee.conf -a -p -t "Stack backtrace" -s "host" $HOSTNAME -s "path" $working_dir >/dev/null
		fi

		if [ -f lastcore.log ]; then
			tail -50 lastcore.log | ./slacktee.sh --config slacktee.conf -a -p -t "Log file" -s "host" $HOSTNAME -s "path" $working_dir >/dev/null
		fi
	fi
}

function append_eventlog() {
	echo -n "$(get_datetime)"

	for var in "$@"; do
		echo -n "|$var"
	done

	echo ""
} >> eventlog

function backup_core() {
	core_file=$(find_newest_file core*)
	if [ ! -z ${core_file} ] && [ -f ${core_file} ]; then
		# process core file
		gdb --batch --quiet -ex "bt" -ex "quit" ./gb ${core_file} 2>&1 | grep -v "^\[" > lastcore.bt.txt

		# check if it's our core
		grep -q "generated by .*\./gb" lastcore.bt.txt
		IS_GB_CORE=$?

		grep -q "warning: exec file is newer than core file" lastcore.bt.txt
		NOT_CURRENT_CORE=$?

		if [ $IS_GB_CORE -eq 0 ] && [ $NOT_CURRENT_CORE -ne 0 ]; then
			output_file=lastcore.bt-bak$(date -u +%Y%m%d-%H%M%S).txt

			# start backtrace with gb version so we can match line numbers to a specific version if need to
			./gb -v > ${output_file}
			gdb --batch --quiet -ex "thread apply all bt full" -ex "quit" ./gb ${core_file} >> ${output_file} 2>/dev/null
			mv ${core_file} lastcore.core

			# we only keep one copy to avoid filling up the disk if dumping repeatedly..
			cp -p gb lastcore.gb

			# cp not mv to avoid potentially overwriting logs
			cp -p $(find_newest_file log*) lastcore.log

			return 0
		else
			if [ $NOT_CURRENT_CORE -eq 0 ]; then
				mv ${core_file} lastcore.${core_file}
			else
				mv ${core_file} lastcore.notgb.core
			fi
		fi
	fi

	return 1
}

function backup_core_with_alert() {
	if backup_core; then
		if [ $# -gt 0 ]; then
			send_alert "$@"
		fi
		send_core_alert
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
backup_core_with_alert "Core found at startup"


##################################
# verify system converters exist #
##################################

function verify_converter() {
	which $1 >/dev/null 2>&1
	WHICHSTATUS=$?

	if [ $WHICHSTATUS -ne 0 ]; then
		send_alert "Missing $1 converter. Not starting GB"
		exit $WHICHSTATUS
	fi
}

# pdf
verify_converter "pdftohtml"


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

GB_VERSION=$(get_gb_version)

while true; do
	# in case gb was updated
	if [ -f gb.installed ]; then
		mv -f gb.installed gb
		GB_VERSION=$(get_gb_version)
	fi

	# leftover from previous run
	rm -f cleanexit

	GB_PRE=
	cpu_affinity=$(get_cpu_affinity)
	if [ ! -z ${cpu_affinity} ]; then
		GB_PRE="taskset -c ${cpu_affinity}"
	fi

	# rename if exist
	if [ -f file_state.txt ]; then
		mv file_state.txt file_state-bak$(date -u +%Y%m%d-%H%M%S).txt
	fi

	if [ -s gb_output.txt ]; then
		mv gb_output.txt gb_output-bak$(date -u +%Y%m%d-%H%M%S).txt
	fi

	# Dump list of files before allowing gb to continue running
	find . -not -path '*/\.*' -not -path '*/__*' -not -path './*-bak*' -type f -exec ls -l --full-time {} \; 2>/dev/null |column -t|sort -k 9 > file_state.txt

	GB_START_TIME=$(date +%s)
	append_eventlog "gb start" "${GB_VERSION}"

	${GB_PRE} ./gb -l $ADDARGS > gb_output.txt 2>&1
	EXITSTATUS=$?

	GB_END_TIME=$(date +%s)
	GB_UPTIME=$(expr ${GB_END_TIME} - ${GB_START_TIME})

	# if gb does exit(0) then stop
	# also stop if ./cleanexit is there because exit(0) does not always work for some strange reasons
	if [ $EXITSTATUS = 0 ] || [ -f "cleanexit" ]; then
		append_eventlog "gb stop" "${GB_VERSION}"
		break
	fi

	# stop if ./fatal_error is there
	if [ -f "fatal_error" ]; then
		append_eventlog "gb fatal error" "${GB_VERSION}"
		send_alert "FATAL ERROR. Shut down"
		backup_core_with_alert
		break
	fi

	# alert even if core doesn't exist
	append_eventlog "gb core dumped" "${GB_VERSION}"
	send_alert "GB died, core dumped"
	backup_core_with_alert


	if [ $GB_UPTIME -le 300 ]; then
		send_alert "GB uptime less than 5 minutes. Not restarting GB"
		break
	fi

	ADDARGS='-r'$INC
	INC=$((INC+1))
done > /dev/null 2>&1 &

