#!/usr/bin/env bash

rm -rf coll.main.?
rm -f *-saved.dat spiderproxystats.dat addsinprogress.dat robots.txt.cache dns.cache rebalance.txt
rm -f fatal_error
rm -f urlblacklist.txt urlwhitelist.txt dnsblocklist.txt contenttypeblocklist.txt contenttypeallowed.txt
rm -f ipblocklist.txt
rm -f docdelete.txt docdelete.txt.processing docdelete.txt.lastpos
rm -f docrebuild.txt docrebuild.txt.processing docrebuild.txt.lastpos
rm -f docreindex.txt docreindex.txt.processing docreindex.txt.lastpos
rm -f spiderdbhostdelete.txt spiderdbhostdelete.txt.processing
rm -f spiderdburldelete.txt spiderdburldelete.txt.processing
rm -f explicit_keywords.txt
rm -f adultwords.txt adultphrases.txt spamphrases.txt
rm -f robotschecklist.txt urlresultoverride.txt

