SHELL = /bin/bash

config=release

uname_m = $(shell uname -m)
ARCH=$(uname_m)

BASE_DIR=$(shell pwd)
export BASE_DIR

OBJS =  UdpSlot.o Rebalance.o \
	Msg13.o \
	PageGet.o PageHosts.o \
	PageParser.o PageInject.o PagePerf.o PageReindex.o PageResults.o \
	PageAddUrl.o PageRoot.o PageSockets.o PageStats.o \
	PageTitledb.o \
	PageAddColl.o \
	PageHealthCheck.o \
	hash.o Domains.o \
	Collectiondb.o \
	linkspam.o ip.o sort.o \
	fctypes.o XmlNode.o XmlDoc.o XmlDoc_Indexing.o Xml.o \
	Words.o UdpServer.o \
	Titledb.o HashTable.o \
	TcpServer.o Summary.o \
	Spider.o SpiderColl.o SpiderLoop.o Doledb.o \
	RdbTree.o RdbScan.o RdbMerge.o RdbMap.o RdbMem.o RdbBuckets.o \
	RdbList.o RdbDump.o RdbCache.o Rdb.o RdbBase.o \
	Query.o Phrases.o Multicast.o \
	Msg5.o \
	Msg39.o Msg3.o \
	Msg22.o \
	Msg20.o Msg2.o \
	Msg1.o \
	Msg0.o Mem.o Matches.o Loop.o \
	Log.o Lang.o \
	Posdb.o PosdbTable.o \
	Clusterdb.o \
	HttpServer.o HttpRequest.o \
	HttpMime.o Hostdb.o \
	Highlight.o File.o Errno.o Entities.o \
	Dns.o Dir.o Conf.o Bits.o \
	Stats.o BigFile.o \
	Speller.o \
	PingServer.o StopWords.o TopTree.o \
	Parms.o Pages.o \
	Unicode.o iana_charset.o \
	SearchInput.o \
	SafeBuf.o \
	UCPropTable.o UnicodeProperties.o \
	Pops.o Title.o Pos.o \
	Profiler.o \
	Msg3a.o HashTableT.o HashTableX.o \
	PageLogView.o Msg1f.o Blaster.o MsgC.o \
	Proxy.o PageThreads.o Linkdb.o \
	matches2.o LanguageIdentifier.o \
	Repair.o Process.o \
	Abbreviations.o \
	Msg51.o \
	Msg40.o Msg4.o SpiderProxy.o \
	Statsdb.o PageStatsdb.o \
	Msge0.o Msge1.o \
	CountryCode.o DailyMerge.o Tagdb.o \
	Images.o Wiki.o Wiktionary.o \
	Timezone.o Sections.o SiteGetter.o \
	Synonyms.o \
	PageCrawlBot.o Json.o PageBasic.o \
	Punycode.o Version.o \
	HighFrequencyTermShortcuts.o \
	IPAddressChecks.o \
	SummaryCache.o \
	ScalingFunctions.o \
	RobotRule.o Robots.o \
	JobScheduler.o \
	AdultCheck.o \
	Url.o UrlParser.o UrlComponent.o \
	Statistics.o \
	Sanity.o \

# common flags
DEFS = -D_REENTRANT_ -I.
CPPFLAGS = -g -fno-stack-protector -DPTHREADS -Wstrict-aliasing=0
CPPFLAGS += -std=c++11

# optimization
ifeq ($(config),$(filter $(config),debug test coverage))
O1 =
O2 =
O3 =

else ifeq ($(config),$(filter $(config),release sanitize))
O1 = -O1
O2 = -O2
O3 = -O3

ifeq ($(ARCH), x86_64)
CPPFLAGS += -march=core-avx-i -msse4.2
endif

endif

# defines
ifeq ($(config),debug)
DEFS += -D_VALGRIND_

else ifeq ($(config),test)
DEFS += -D_VALGRIND_
DEFS += -DPRIVACORE_TEST_VERSION

else ifeq ($(config),coverage)
CONFIG_CPPFLAGS += --coverage

else ifeq ($(config),sanitize)
DEFS += -DPRIVACORE_SAFE_VERSION
CONFIG_CPPFLAGS += -fsanitize=address -fno-omit-frame-pointer # libasan
CONFIG_CPPFLAGS += -fsanitize=undefined # libubsan
#CONFIG_CPPFLAGS += -fsanitize=thread # libtsan
CONFIG_CPPFLAGS += -fsanitize=leak # liblsan

else ifeq ($(config),release)
# if defined, UI options that can damage our production index will be disabled
DEFS += -DPRIVACORE_SAFE_VERSION

endif

# export to sub-make
export CONFIG_CPPFLAGS

CPPFLAGS += $(CONFIG_CPPFLAGS)

ifeq ($(CXX), g++)
CPPFLAGS += -Wall
CPPFLAGS += -Wno-write-strings -Wno-maybe-uninitialized -Wno-unused-but-set-variable
CPPFLAGS += -Wno-invalid-offsetof

else ifeq ($(CXX), clang++)
CPPFLAGS += -Weverything

# disable offsetof warnings
CPPFLAGS += -Wno-invalid-offsetof -Wno-extended-offsetof

# extensions
CPPFLAGS += -Wno-gnu-zero-variadic-macro-arguments -Wno-gnu-conditional-omitted-operand
CPPFLAGS += -Wno-zero-length-array -Wno-c99-extensions

# other warnings (to be moved above or re-enabled when we have cleaned up the code sufficiently)
CPPFLAGS += -Wno-cast-align -Wno-tautological-undefined-compare -Wno-float-equal -Wno-weak-vtables -Wno-global-constructors -Wno-exit-time-destructors
CPPFLAGS += -Wno-shadow -Wno-conversion -Wno-sign-conversion -Wno-old-style-cast -Wno-shorten-64-to-32
CPPFLAGS += -Wno-unused-parameter -Wno-missing-prototypes
CPPFLAGS += -Wno-sometimes-uninitialized -Wno-conditional-uninitialized
CPPFLAGS += -Wno-packed -Wno-padded
CPPFLAGS += -Wno-c++98-compat-pedantic
CPPFLAGS += -Wno-writable-strings -Wno-c++11-compat-deprecated-writable-strings
CPPFLAGS += -Wno-deprecated

endif

LIBS = -lm -lpthread -lssl -lcrypto -lz

# to build static libiconv.a do a './configure --enable-static' then 'make' in the iconv directory

# platform specific flags
ifeq ($(ARCH), i686)
CPPFLAGS += -m32
LIBS += ./libiconv.a

else ifeq ($(ARCH), i386)
CPPFLAGS += -m32
LIBS +=  ./libiconv.a

else ifeq ($(ARCH), x86_64)
CPPFLAGS +=
LIBS += ./libiconv64.a

else ifeq ($(ARCH), armv7l)
CPPFLAGS += -fsigned-char

else
CPPFLAGS +=
LIBS += ./libiconv64.a
endif


# generate git version
DIRTY=
ifneq ($(shell git diff --shortstat 2> /dev/null),)
	DIRTY=-dirty
endif
GIT_VERSION=$(shell git rev-parse HEAD)$(DIRTY)


.PHONY: all
all: gb


# third party libraries
LIBFILES = libcld2_full.so slacktee.sh
LIBS += -Wl,-rpath=. -L. -lcld2_full


libcld2_full.so:
	cd third-party/cld2/internal && CPPFLAGS="-ggdb -std=c++98" ./compile_libs.sh
	ln -s third-party/cld2/internal/libcld2_full.so libcld2_full.so


slacktee.sh:
	ln -sf third-party/slacktee/slacktee.sh slacktee.sh 2>/dev/null


.PHONY: vclean
vclean:
	rm -f Version.o


gb: vclean $(OBJS) main.o $(LIBFILES)
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ main.o $(OBJS) $(LIBS)


.PHONY: static
static: vclean $(OBJS) main.o $(LIBFILES)
	$(CXX) $(DEFS) $(CPPFLAGS) -static -o gb main.o $(OBJS) $(LIBS)


# use this for compiling on CYGWIN:
# only for 32bit cygwin right now and
# you have to install the packages that have these libs.
# you have to get these packages from cygwin:
# 1. LIBS  > zlib-devel: Gzip de/compression library (development)
# 2. LIBS  > libiconv: GNU character set conversion library and utlities

# 3. DEVEL > openssl: cygwin32-openssl: OpenSSL for Cygwin 32bit toolchain

# 3. NET   > openssl: A general purpose cryptographt toolkit with TLS impl...

# 4. DEVEL > mingw-pthreads: Libpthread for MinGW.org Wind32 toolchain
# 5. DEVEL > gcc-g++: GNU Compiler Collection (C++)
# 6. DEVEL > make: The GNU version of the 'make' utility
# 7. DEVEL > git: Distributed version control system
# 8. EDITORS > emacs
.PHONY: cygwin
cygwin:
	make DEFS="-DCYGWIN -D_REENTRANT_ -I." LIBS=" -lz -lm -lpthread -lssl -lcrypto -liconv" gb


.PHONY: gb32
gb32:
	make CPPFLAGS="-m32 -g -Wall -pipe -fno-stack-protector -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized -DPTHREADS -Wno-unused-but-set-variable" LIBS=" -L. ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libstdc++.a -lpthread " gb


.PHONY: dist
dist: DIST_DIR=gb-$(shell date +'%Y%m%d')-$(shell git rev-parse --short HEAD)
dist: all
	@mkdir $(DIST_DIR)
	@cp -prL ucdata/ \
	antiword \
	antiword-dir/ \
	html/ \
	pstotext \
	gb.pem \
	gb \
	gbstart.sh \
	gbconvert.sh \
	libcld2_full.so \
	pnmscale \
	libnetpbm.so.10 \
	bmptopnm \
	giftopnm \
	jpegtopnm \
	ppmtojpeg \
	libjpeg.so.62 \
	pngtopnm \
	libpng12.so.0 \
	tifftopnm \
	libtiff.so.4 \
	LICENSE \
	mysynonyms.txt \
	wikititles.txt.part1 \
	wikititles.txt.part2 \
	wiktionary-buf.txt \
	wiktionary-lang.txt \
	wiktionary-syns.dat \
	sitelinks.txt \
	unifiedDict.txt \
	valgrind.cfg \
	.valgrindrc \
	$(DIST_DIR)
	@cp third-party/cld2/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-CLD2
	@cp third-party/slacktee/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-SLACKTEE
	@tar -czvf $(DIST_DIR).tar.gz $(DIST_DIR)
	@rm -rf $(DIST_DIR)


# doxygen
doc:
	doxygen doxygen/doxygen_config.conf


# used for unit testing
libgb.a: $(OBJS)
	ar rcs $@ $^


.PHONY: test
test: unittest systemtest


.PHONY: unittest
unittest:
	+$(MAKE) -C test $@


.PHONY: systemtest
systemtest:
	$(MAKE) -C test $@


.PHONY: clean
clean:
	-rm -f *.o gb core core.* libgb.a
	-rm -f gmon.*
	-rm -f *.gcda *.gcno coverage*.html
	-rm -f *.ll *.ll.out pstack.txt
	-rm entities.inc
	$(MAKE) -C test $@


.PHONY: cleandb
cleandb:
	rm -rf coll.main.?
	rm -f *-saved.dat spiderproxystats.dat addsinprogress.dat robots.txt.cache dns.cache


# shortcuts
.PHONY: debug
debug:
	$(MAKE) config=debug


# pip install gcovr

.PHONY: coverage
coverage:
	$(MAKE) config=coverage unittest
	gcovr -r . --html --html-details --branch --output=coverage.html --exclude=".*Test\.cpp" --exclude="googletest.*"


# special dependency and auto-generated file
Entities.o: entities.inc

entities.inc: entities.json generate_entities.py
entities.json entities.inc:
	./generate_entities.py >entities.inc

StopWords.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Loop.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

hash.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

fctypes.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Matches.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Highlight.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

matches2.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

linkspam.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# Url::set() seems to take too much time
Url.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp

UrlParser.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

UrlComponent.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

# when making a new file, add the recs to the map fast
RdbMap.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

# this was getting corruption, was it cuz we used $(O2) compiler option?
# RdbTree.o:
# 	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

RdbBuckets.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

Linkdb.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

XmlDoc.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# final gigabit generation in here:
Msg40.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

TopTree.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

UdpServer.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

RdbList.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

Rdb.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# take this out. seems to not trigger merges when percent of
# negative titlerecs is over 40.
RdbBase.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# RdbCache.cpp gets "corrupted" with $(O2)... like RdbTree.cpp
#RdbCache.o:
#	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# fast dictionary generation and spelling recommendations
#Speller.o:
#	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Posdb.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

PosdbTable.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# Query::setBitScores() needs this optimization
#Query.o:
#	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Msg3.o Msg2.o Msg5.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# fast parsing
Xml.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
XmlNode.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Words.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Unicode.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
UCPropTable.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
UnicodeProperties.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Pos.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Pops.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Bits.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Sections.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Summary.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp
Title.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

SafeBuf.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

Profiler.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

HashTableT.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

HashTableX.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

# getUrlFilterNum2()
Spider.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

SpiderColl.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

SpiderLoop.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

Doledb.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

PostQueryRerank.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O2) -c $*.cpp

sort.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

IPAddressChecks.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

Statistics.o:
	$(CXX) $(DEFS) $(CPPFLAGS) $(O3) -c $*.cpp

Version.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -DGIT_COMMIT_ID=$(GIT_VERSION) -DBUILD_CONFIG=$(config) -c $*.cpp

install:
# gigablast will copy over the necessary files. it has a list of the
# necessary files and that list changes over time so it is better to let gb
# deal with it.
	mkdir -p $(DESTDIR)/var/gigablast/data0/
	mkdir -p $(DESTDIR)/usr/bin/
	mkdir -p $(DESTDIR)/etc/init.d/
	mkdir -p $(DESTDIR)/etc/init/
	mkdir -p $(DESTDIR)/etc/rc3.d/
	mkdir -p $(DESTDIR)/lib/init/
	./gb copyfiles $(DESTDIR)/var/gigablast/data0/
# if user types 'gb' it will use the binary in /var/gigablast/data0/gb
	rm -f $(DESTDIR)/usr/bin/gb
	ln -s /var/gigablast/data0/gb $(DESTDIR)/usr/bin/gb
	cp S99gb $(DESTDIR)/etc/init.d/gb
	ln -s /etc/init.d/gb $(DESTDIR)/etc/rc3.d/S99gb

.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp

.c.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.c

.PHONY: depend
depend: entities.inc
	@echo "generating dependency information"
	$(CXX) -MM $(DEFS) $(DPPFLAGS) *.cpp > Make.depend

-include Make.depend

