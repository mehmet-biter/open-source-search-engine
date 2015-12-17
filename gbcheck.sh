#!/usr/bin/env bash

working_dir=$(dirname $0)

# Don't allow startup if fatal_error file exists
if [ -f ${working_dir}/fatal_error ]; then
    exit 1
fi

# verify system converters exist

# pdf
which pdftohtml >/dev/null 2>&1 || exit $?
