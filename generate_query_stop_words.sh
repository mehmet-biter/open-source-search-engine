#!/bin/bash

if [ $# -ne 3 ]; then
	echo "`basename $0`: usage: <langcode> <inputfile> <outputfile>" >&2
	exit 99
fi

echo "static const char *s_query_stop_words_$1[] = {" >$3

cat $2 |
tr -d ' 	,' |
cut -d/ -f1 |
egrep -v '^$' |
awk '{print "	\"" $1 "\","}' |
cat - >>$3

echo "};" >>$3
