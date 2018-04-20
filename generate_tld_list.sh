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
#  2:	Official or semi-official 2nd-level domains: wikipedia, with manual plausability checks. Mozilla's list with manual checks.
#	File: official_2nd_level_domains.txt
#  3:	Unofficial or defacto 2nd-level domains: Manual checks (with help from domain-crunching scripts)
#	File: additional_2nd_level_domains.txt (optional)
#
#This is not perfect. Eg. .va is a ccTLD but it isn't exactly independent entities underneath that domain. 

die() {
	echo "$*" >&2
	exit 1
}

[ $# -ne 1 ] && die "Usage: `basename $0` <dstfile>"

if [ ! -r tlds-alpha-by-domain.txt ]; then
	die "File 'tlds-alpha-by-domain.txt' is missing/unreadable"
fi

if [ ! -r official_2nd_level_domains.txt ]; then
	die "File 'official_2nd_level_domains.txt' is missing/unreadable"
fi



TMPFILE1=/tmp/$$.file1
cat tlds-alpha-by-domain.txt > $TMPFILE1 || die
cat official_2nd_level_domains.txt >> $TMPFILE1 || die
if [ -r additional_2nd_level_domains.txt ]; then
	cat additional_2nd_level_domains.txt >> $TMPFILE1 || die
fi


#extract only the domains, then uppercase it, sort, unique, 
TMPFILE2=/tmp/$$.file2
cat $TMPFILE1 |
cut -d'#' -f1 |
sed -e 's/^ *//g' -e 's/ *$//g' |
grep -v -e '^$' |
tr '[[:lower:]]' '[[:upper:]]' |
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
