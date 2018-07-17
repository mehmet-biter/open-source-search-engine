#!/bin/bash

#Generate TLD list for use in Domains.cpp
#
#By "TLD" we don't mean TLD. We mean "1/2/3/...-level domain that has seprate entities underneath it"
#Eg.
#    .com is a TLD with multiple entities underneath it, eg. microsoft.com and ibm.com
#    .no is a ccTLD with multiple entities underneath it, eg vg.no and aftenposten.no
#    .co.uk is a 2nd-level domain with multiple entities underneath it, eg bbc.co.uk and itn.co.uk
#
#sources for a complete list:
#  1:	TLDs and ccTLDs: https://data.iana.org/TLD/tlds-alpha-by-domain.txt
#	File: tlds-alpha-by-domain.txt
#  2:	Official or semi-official 2nd-level domains: wikipedia, with manual plausability checks. Mozilla's list (https://wiki.mozilla.org/TLD_List) with manual checks.
#	File: tlds-official-2nd-level-domains.txt
#  3:	Unofficial or defacto 2nd-level domains: Manual checks (with help from domain-crunching scripts)
#	File: tlds-additional-2nd-level-domains.txt (optional)
#
#This is not perfect. Eg. .va is a ccTLD but it isn't exactly independent entities underneath that domain. 

die() {
	echo "$*" >&2
	exit 1
}

[ $# -ne 1 ] && die "Usage: `basename $0` <dstfile>"

TLDS_ICANN_FILENAME=tlds-alpha-by-domain.txt
TLDS_OFFICIAL_SLD_FILENAME=tlds-official-2nd-level-domains.txt
TLDS_ADDITIONAL_SLD_FILENAME=tlds-additional-2nd-level-domains.txt

if [ ! -r ${TLDS_ICANN_FILENAME} ]; then
	die "File '${TLDS_ICANN_FILENAME}' is missing/unreadable"
fi

if [ ! -r ${TLDS_OFFICIAL_SLD_FILENAME} ]; then
	die "File '${TLDS_OFFICIAL_SLD_FILENAME}' is missing/unreadable"
fi



TMPFILE1=/tmp/$$.file1
cat ${TLDS_ICANN_FILENAME} > $TMPFILE1 || die
cat ${TLDS_OFFICIAL_SLD_FILENAME} >> $TMPFILE1 || die
if [ -r ${TLDS_ADDITIONAL_SLD_FILENAME} ]; then
	cat ${TLDS_ADDITIONAL_SLD_FILENAME} >> $TMPFILE1 || die
fi


#extract only the domains, then lowercase it, sort, unique, put into tmpfile2
TMPFILE2=/tmp/$$.file2
cat $TMPFILE1 |
cut -d'#' -f1 |
sed -e 's/^ *//g' -e 's/ *$//g' |
grep -v -e '^$' |
tr '[[:upper:]]' '[[:lower:]]' |
sort |
uniq > $TMPFILE2 || die

rm $TMPFILE1

TMPFILE3=/tmp/$$.file3
echo "static const char * const s_tlds[] = {" >$TMPFILE3 || die
cat $TMPFILE2 |sed -e 's/^.*$/	"\0",/g' >>$TMPFILE3 || die
echo "	NULL" >>$TMPFILE3 || die
echo "};" >>$TMPFILE3 || die

rm $TMPFILE2

cp $TMPFILE3 $1 || die
rm $TMPFILE3

exit 0
