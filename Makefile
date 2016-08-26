SHELL = /bin/bash

config=release

uname_m = $(shell uname -m)
ARCH=$(uname_m)

BASE_DIR=$(shell pwd)
export BASE_DIR

unexport CONFIG_CPPFLAGS

OBJS_O0 =  \
	Abbreviations.o AdultCheck.o \
	BigFile.o Blaster.o \
	Clusterdb.o Collectiondb.o Conf.o CountryCode.o \
	DailyMerge.o Dir.o Dns.o Domains.o \
	Errno.o Entities.o \
	File.o \
	GbMutex.o \
	HashTable.o HighFrequencyTermShortcuts.o HttpMime.o HttpRequest.o HttpServer.o Hostdb.o \
	iana_charset.o Images.o ip.o \
	JobScheduler.o Json.o \
	Lang.o LanguageIdentifier.o Log.o \
	Mem.o Msg0.o Msg1.o Msg4.o MsgC.o Msg13.o Msg20.o Msg22.o Msg39.o Msg1f.o Msg3a.o Msg51.o Msge0.o Msge1.o Multicast.o \
	Parms.o Pages.o PageAddColl.o PageAddUrl.o PageBasic.o PageCrawlBot.o PageGet.o PageHealthCheck.o PageHosts.o PageInject.o PageLogView.o \
	PageParser.o PagePerf.o PageReindex.o PageResults.o PageRoot.o PageSockets.o PageStats.o PageStatsdb.o PageThreads.o PageTitledb.o \
	Phrases.o PingServer.o Process.o Proxy.o Punycode.o \
	Query.o \
	RdbCache.o RdbDump.o RdbMem.o RdbMerge.o RdbScan.o RdbTree.o \
	Rebalance.o Repair.o RobotRule.o Robots.o \
	Sanity.o ScalingFunctions.o SearchInput.o SiteGetter.o Speller.o SpiderProxy.o Stats.o Statsdb.o SummaryCache.o Synonyms.o \
	Tagdb.o TcpServer.o Timezone.o Titledb.o \
	Version.o \
	Wiki.o Wiktionary.o \
	UdpSlot.o Url.o \


OBJS_O1 = \


OBJS_O2 = \
	Bits.o \
	Doledb.o \
	fctypes.o \
	hash.o HashTableT.o HashTableX.o Highlight.o \
	linkspam.o Loop.o \
	Matches.o matches2.o Msg2.o Msg3.o Msg5.o \
	Pops.o Pos.o Posdb.o PosdbTable.o Profiler.o \
	Rdb.o RdbBase.o \
	Sections.o Spider.o SpiderColl.o SpiderLoop.o StopWords.o Summary.o \
	Title.o \
	UCPropTable.o UdpServer.o Unicode.o UnicodeProperties.o \
	Words.o \
	Xml.o XmlDoc.o XmlDoc_Indexing.o XmlNode.o \


OBJS_O3 = \
	IPAddressChecks.o \
	Linkdb.o \
	Msg40.o \
	RdbBuckets.o RdbIndex.o RdbIndexQuery.o RdbList.o RdbMap.o \
	SafeBuf.o sort.o Statistics.o \
	TopTree.o \
	UrlComponent.o UrlParser.o UdpStatistic.o \


OBJS = $(OBJS_O0) $(OBJS_O1) $(OBJS_O2) $(OBJS_O3)


# RdbTree.cpp was getting corruption, was it cuz we used $(O2) compiler option?
# RdbCache.cpp gets "corrupted" with $(O2)... like RdbTree.cpp


# common flags
DEFS = -D_REENTRANT_ -I.
CPPFLAGS = -g -fno-stack-protector -DPTHREADS
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

CPPFLAGS += $(CONFIG_CPPFLAGS)

# dependencies
CPPFLAGS += -MMD -MP

# export to sub-make
export CONFIG_CPPFLAGS

ifeq ($(CXX), g++)
CPPFLAGS += -Wall

# disable offsetof warnings
CPPFLAGS += -Wno-invalid-offsetof

CPPFLAGS += -Wstrict-aliasing=0
CPPFLAGS += -Wno-write-strings
CPPFLAGS += -Wno-maybe-uninitialized
CPPFLAGS += -Wno-unused-but-set-variable

else ifeq ($(CXX), clang++)
CPPFLAGS += -Weverything

# enable colour
CPPFLAGS += -fcolor-diagnostics

# disable offsetof warnings
CPPFLAGS += -Wno-invalid-offsetof -Wno-extended-offsetof

# extensions
CPPFLAGS += -Wno-gnu-zero-variadic-macro-arguments -Wno-gnu-conditional-omitted-operand
CPPFLAGS += -Wno-zero-length-array -Wno-c99-extensions

# compability (we're using c++11)
CPPFLAGS += -Wno-c++98-compat-pedantic

# other warnings we don't care about
CPPFLAGS += -Wno-format-pedantic

# other warnings (to be moved above or re-enabled when we have cleaned up the code sufficiently)
CPPFLAGS += -Wno-cast-align -Wno-tautological-undefined-compare -Wno-float-equal -Wno-weak-vtables -Wno-global-constructors -Wno-exit-time-destructors
CPPFLAGS += -Wno-shadow -Wno-conversion -Wno-sign-conversion -Wno-old-style-cast -Wno-shorten-64-to-32 -Wno-double-promotion
CPPFLAGS += -Wno-unused-parameter -Wno-missing-prototypes
CPPFLAGS += -Wno-sometimes-uninitialized -Wno-conditional-uninitialized
CPPFLAGS += -Wno-packed -Wno-padded
CPPFLAGS += -Wno-writable-strings
CPPFLAGS += -Wno-deprecated
CPPFLAGS += -Wno-reserved-id-macro -Wno-unused-macros
CPPFLAGS += -Wno-missing-field-initializers
CPPFLAGS += -Wno-covered-switch-default
CPPFLAGS += -Wno-date-time

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
GIT_BRANCH=$(shell git rev-parse --abbrev-ref HEAD)


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


# used for tools/unittest
libgb.a: $(OBJS)
	ar rcs $@ $^

.PHONY: tools
tools:
	+$(MAKE) -C tools

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
	-rm -f *.o *.d gb core core.* libgb.a
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
entities.inc: entities.json generate_entities.py
entities.json entities.inc:
	./generate_entities.py >entities.inc

Entities.o: entities.inc
Version.o: CPPFLAGS += -DGIT_COMMIT_ID=$(GIT_VERSION) -DGIT_BRANCH=$(GIT_BRANCH) -DBUILD_CONFIG=$(config)

# different optimization level
$(OBJS_O1): CPPFLAGS += $(O1)
$(OBJS_O2): CPPFLAGS += $(O2)
$(OBJS_O3): CPPFLAGS += $(O3)

.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp

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


DEPS := $(OBJS:.o=.d)

-include $(DEPS)

