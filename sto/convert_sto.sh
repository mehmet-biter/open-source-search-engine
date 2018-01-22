#!/bin/bash

bd=`dirname $0`

if [ $# -ne 2 ]; then
	echo "`basename $0`: usage: <sto-directory> <output-file>" >&2
	exit 1
fi

if [ -f "$2" ]; then
	rm $2 || exit
fi


echo "===Making signature"
$bd/sto_convert.py signature --output_file="$2" || exit
for input_file in $1/STO_LMF_morphology_{adj,noun,pronoun,rest,verb}*.xml; do
	echo "===Processing $input_file"
	$bd/sto_convert.py convert --input_file=$input_file --output_file=$2 || exit
done
echo "===Done"
exit 0
