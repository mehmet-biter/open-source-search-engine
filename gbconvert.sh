#!/usr/bin/env bash

convert_file() {
    # read input
    working_dir=$(dirname $0)
	filetype=$1
	input=$2
	output=$3

	case "${filetype}" in
	html)
		./browser.py --input-file ${input} > "${output}"
		exit $?
		;;
	pdf)
		pdftohtml -q -i -noframes -enc UTF-8 -stdout "${input}" > "${output}"
		exit $?
		;;
	doc)
	    ulimit -v 25000
	    ANTIWORDHOME=${working_dir}/antiword-dir ${working_dir}/antiword "${input}" > "${output}"
	    exit $?
	    ;;
	xls)
	    #${working_dir}/xlhtml "${input}" > "${output}"
	    exit 69 # EX_UNAVAILABLE
	    ;;
	ppt)
	    #${working_dir}/pphtml "${input}" > "${output}"
	    exit 69 # EX_UNAVAILABLE
	    ;;
	ps)
	    ulimit -v 25000
	    ${working_dir}/pstotext "${input}" > "${output}"
	    exit $?
	    ;;
	*)
		exit 69 # EX_UNAVAILABLE
		;;
	esac
}

# verify input
if [ $# -ne 3 ]; then
    exit 64 # EX_USAGE
fi

# export function
export -f convert_file

# set limit
ulimit -t 30

# run command
timeout 30s nice -n 19 bash -c 'convert_file "$@"' $0 $@
