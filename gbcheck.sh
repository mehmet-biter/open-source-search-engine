#!/usr/bin/env bash

working_dir=$(dirname $0)

# Don't allow startup if fatal_error file exists
if [ -f ${working_dir}/fatal_error ]; then
    exit 1
fi

# Dump list of files before allowing gb to continue running
find . -not -path '*/\.*' -type f -exec ls -l --full-time {} \; 2> /dev/null |column -t|sort -k 9 > file_state_before_run.txt


# verify system converters exist

# pdf
which pdftohtml >/dev/null 2>&1 || exit $?
