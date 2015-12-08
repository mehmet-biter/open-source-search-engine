#!/usr/bin/env bash

# verify system converters exist

# pdf
which pdftohtml >/dev/null 2>&1 || exit $?
