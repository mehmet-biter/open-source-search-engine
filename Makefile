SHELL = /bin/bash

config=release

uname_m = $(shell uname -m)
ARCH=$(uname_m)

BASE_DIR=$(shell pwd)
export BASE_DIR

unexport CONFIG_CPPFLAGS

OBJS_O0 =  \
	Abbreviations.o \
	BigFile.o \
	Clusterdb.o Collectiondb.o Conf.o CountryCode.o \
	DailyMerge.o Dir.o Dns.o Domains.o \
	Errno.o Entities.o \
	File.o \
	FxTermCheckList.o FxCheckAdult.o FxCheckSpam.o \
	GbMutex.o \
	HashTable.o HighFrequencyTermShortcuts.o PageTemperatureRegistry.o SiteMedianPageTemperatureRegistry.o Docid2Siteflags.o HttpMime.o HttpRequest.o HttpServer.o Hostdb.o \
	iana_charset.o Images.o ip.o \
	JobScheduler.o Json.o \
	Lang.o Log.o \
	Mem.o Msg0.o Msg4In.o Msg4Out.o MsgC.o Msg13.o Msg20.o Msg22.o Msg39.o Msg3a.o Msg51.o Msge0.o Msge1.o Multicast.o \
	Parms.o Pages.o PageAddColl.o PageAddUrl.o PageBasic.o PageCrawlBot.o PageGet.o PageHealthCheck.o PageHosts.o PageInject.o \
	PageParser.o PagePerf.o PageReindex.o PageResults.o PageRoot.o PageSockets.o PageStats.o PageThreads.o PageTitledb.o PageLinkdbLookup.o PageSpiderdbLookup.o PageSpider.o PageDoledbIPTable.o PageDocProcess.o \
	Phrases.o HostFlags.o Process.o Proxy.o Punycode.o \
	Query.o \
	RdbCache.o RdbDump.o RdbMem.o RdbMerge.o RdbScan.o RdbTree.o \
	Rebalance.o Repair.o RobotRule.o Robots.o \
	SpiderdbSqlite.o \
	SpiderdbRdbSqliteBridge.o \
	DumpSpiderdbSqlite.o \
	Sanity.o ScalingFunctions.o SearchInput.o SiteGetter.o Speller.o SpiderProxy.o Stats.o SummaryCache.o Synonyms.o \
	Tagdb.o TcpServer.o Titledb.o \
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
	Sections.o Spider.o SpiderCache.o SpiderColl.o SpiderLoop.o StopWords.o Summary.o \
	Title.o \
	UdpServer.o \
	Xml.o XmlDoc.o XmlDoc_Indexing.o XmlNode.o \


OBJS_O3 = \
	MatchList.o \
	ContentMatchList.o ContentTypeBlockList.o CountryLanguage.o \
	DocDelete.o DocProcess.o DocRebuild.o DocReindex.o DnsBlockList.o \
	IPAddressChecks.o IpBlockList.o \
	LanguageResultOverride.o Linkdb.o \
	Msg40.o \
	Msg25.o \
	RdbBuckets.o RdbIndex.o RdbIndexQuery.o RdbList.o RdbMap.o ResultOverride.o RobotsBlockedResultOverride.o RobotsCheckList.o \
	SafeBuf.o sort.o Statistics.o \
	ScoringWeights.o \
	BaseScoringParameters.o \
	TopTree.o \
	UrlMatch.o UrlMatchList.o UrlBlockCheck.o UrlComponent.o UrlParser.o UdpStatistic.o \
	UrlRealtimeClassification.o UrlResultOverride.o \
	WantedChecker.o \
	MergeSpaceCoordinator.o \
	FxExplicitKeywords.o \
	GbMoveFile.o GbMoveFile2.o GbCopyFile.o GbMakePath.o \
	GbUtil.o \
	GbSignature.o \
	GbCompress.o \
	GbRegex.o \
	GbThreadQueue.o \
	GbEncoding.o FxLanguage.o \
	GbDns.o \
	ConvertSpiderdb.o \
	SpiderdbUtil.o \
	FxBlobCache.o \
	FxBlobCacheInstantiation.o \
	InstanceInfoExchange.o \
	ByteOrderMark.o \
	utf8.o utf8_fast.o utf8_convert.o \
	EGStack.o \
	QueryLanguage.o \
	FxClient.o \
	SiteNumInlinks.o \
	SiteMedianPageTemperature.o \
	MemoryMappedFile.o \
	Lexicons.o \
	Lemma.o \
	Serialize.o \


OBJS = $(OBJS_O0) $(OBJS_O1) $(OBJS_O2) $(OBJS_O3)


# RdbTree.cpp was getting corruption, was it cuz we used $(O2) compiler option?
# RdbCache.cpp gets "corrupted" with $(O2)... like RdbTree.cpp


# common flags
DEFS = -D_REENTRANT_ -I. -Ithird-party/compact_enc_det -Ithird-party/c-ares -Ithird-party/sparsepp -Iword_variations -Itokenizer
DEFS += -DDEBUG_MUTEXES
CPPFLAGS = -g -fno-stack-protector -DPTHREADS
CPPFLAGS += -std=c++11


# optimization
ifeq ($(config),$(filter $(config),release release-safe))
O1 = -O1
O2 = -O2
O3 = -O3

ifeq ($(ARCH), x86_64)
CPPFLAGS += -march=core-avx-i -msse4.2
endif

endif

# defines
ifeq ($(config),$(filter $(config),debug debug-safe debug-test))
DEFS += -D_VALGRIND_
endif

ifeq ($(config),$(filter $(config),test debug-test))
DEFS += -DPRIVACORE_TEST_VERSION

else ifeq ($(config),coverage)
CONFIG_CPPFLAGS += --coverage

else ifeq ($(findstring sanitize, $(config)),sanitize)
DEFS += -DPRIVACORE_SAFE_VERSION

ifeq ($(config),sanitize-address)
CONFIG_CPPFLAGS += -fsanitize=address -fno-omit-frame-pointer # libasan
else ifeq ($(config),sanitize-undefined)
CONFIG_CPPFLAGS += -fsanitize=undefined # libubsan
else ifeq ($(config),sanitize-thread)
CONFIG_CPPFLAGS += -fsanitize=thread # libtsan
else ifeq ($(config),sanitize-leak)
CONFIG_CPPFLAGS += -fsanitize=leak # liblsan
endif

else ifeq ($(config),$(filter $(config),release-safe debug-safe))
# if defined, UI options that can damage our production index will be disabled
DEFS += -DPRIVACORE_SAFE_VERSION
endif

CPPFLAGS += $(CONFIG_CPPFLAGS)

# export to sub-make
export CONFIG_CPPFLAGS

ifeq ($(findstring clang++, $(CXX)),clang++)
# dependencies
CPPFLAGS += -MMD -MP

# warnings
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
CPPFLAGS += -Wno-unused-parameter
CPPFLAGS += -Wno-missing-prototypes
CPPFLAGS += -Wno-conditional-uninitialized
CPPFLAGS += -Wno-packed -Wno-padded
CPPFLAGS += -Wno-writable-strings
CPPFLAGS += -Wno-deprecated
CPPFLAGS += -Wno-reserved-id-macro -Wno-unused-macros
CPPFLAGS += -Wno-missing-field-initializers
CPPFLAGS += -Wno-covered-switch-default
CPPFLAGS += -Wno-date-time
CPPFLAGS += -Wno-format-nonliteral
CPPFLAGS += -Wno-zero-as-null-pointer-constant
CPPFLAGS += -Wno-cast-qual
CPPFLAGS += -Wno-switch-enum
CPPFLAGS += -Wno-undefined-func-template

else ifeq ($(findstring g++, $(CXX)),g++)
# dependencies
CPPFLAGS += -MMD -MP

# versions
GCC_VERSION := $(shell $(CXX) -dumpversion)

ifeq ($(GCC_VERSION), $(filter $(GCC_VERSION), 7, 8))
GCC_VERSION := $(shell $(CXX) -dumpfullversion)
endif

GCC_VER_MIN_81 := $(shell ./cmpversiongte $(GCC_VERSION) 8.1  && echo 1 || echo 0)
GCC_VER_MIN_71 := $(shell ./cmpversiongte $(GCC_VERSION) 7.1  && echo 1 || echo 0)
GCC_VER_MIN_61 := $(shell ./cmpversiongte $(GCC_VERSION) 6.1  && echo 1 || echo 0)
GCC_VER_MIN_51 := $(shell ./cmpversiongte $(GCC_VERSION) 5.1  && echo 1 || echo 0)

# warnings
CPPFLAGS += -Wall

# enable more warnings
#CPPFLAGS += -Wformat=2
CPPFLAGS += -Wformat-security

# version specific warnings
ifeq ($(GCC_VER_MIN_81), 1)
CPPFLAGS += -Wmultistatement-macros
CPPFLAGS += -Warray-bounds
CPPFLAGS += -Wrestrict

# other warnings (to be moved above or re-enabled when we have cleaned up the code sufficiently)
CPPFLAGS += -Wno-class-memaccess
endif

ifeq ($(GCC_VER_MIN_71), 1)
CPPFLAGS += -Wduplicated-branches
endif

ifeq ($(GCC_VER_MIN_61), 1)
CPPFLAGS += -Wshift-negative-value -Wshift-overflow=2
CPPFLAGS += -Wduplicated-cond

# other warnings (to be moved above or re-enabled when we have cleaned up the code sufficiently)
CPPFLAGS += -Wno-nonnull-compare
endif

ifeq ($(GCC_VER_MIN_51), 1)
CPPFLAGS += -Wodr
CPPFLAGS += -Wswitch-bool
CPPFLAGS += -Wlogical-not-parentheses
CPPFLAGS += -Wsizeof-array-argument
CPPFLAGS += -Wbool-compare
#CPPFLAGS += -Wsuggest-final-types
#CPPFLAGS += -Wsuggest-final-methods
endif

# disable offsetof warnings
CPPFLAGS += -Wno-invalid-offsetof

# gcc's semantics are sub-optimal
CPPFLAGS += -Wno-unused-result

# other warnings (to be moved above or re-enabled when we have cleaned up the code sufficiently)
CPPFLAGS += -Wstrict-aliasing=0
CPPFLAGS += -Wno-write-strings
CPPFLAGS += -Wno-maybe-uninitialized
CPPFLAGS += -Wno-unused-but-set-variable
CPPFLAGS += -Wno-unused-parameter

endif

LIBS = -lm -lpthread -lssl -lcrypto -lz -lpcre -lsqlite3 -ldl

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
DIST_LIBFILES = libcld2_full.so libcld3.so libced.so libcares.so libcares.so.2 slacktee.sh
LIBFILES = $(DIST_LIBFILES) libword_variations.a libsto.a libtokenizer.a libunicode.a
LIBS += -Wl,-rpath=. -L. -lcld2_full -lcld3 -lprotobuf -lced -lcares
LIBS += -ltokenizer
LIBS += -lword_variations -lsto -lunicode

CLD2_SRC_DIR=third-party/cld2/internal
libcld2_full.so:
	cd $(CLD2_SRC_DIR) && CPPFLAGS="-ggdb -std=c++98" ./compile_libs.sh
	ln -sf $(CLD2_SRC_DIR)/libcld2_full.so libcld2_full.so

CLD3_SRC_DIR=third-party/cld3/src
libcld3.so:
	mkdir -p $(CLD3_SRC_DIR)/cld_3/protos && protoc --proto_path=$(CLD3_SRC_DIR)/ --cpp_out=$(CLD3_SRC_DIR)/cld_3/protos/ $(CLD3_SRC_DIR)/*.proto
	cd $(CLD3_SRC_DIR) && g++ -std=c++11 -shared -fPIC \
	base.cc embedding_feature_extractor.cc embedding_network.cc feature_extractor.cc feature_types.cc fml_parser.cc \
	language_identifier_features.cc lang_id_nn_params.cc nnet_language_identifier.cc registry.cc relevant_script_feature.cc \
	sentence_features.cc script_span/fixunicodevalue.cc script_span/generated_entities.cc script_span/generated_ulscript.cc \
	script_span/getonescriptspan.cc script_span/offsetmap.cc script_span/text_processing.cc script_span/utf8statetable.cc \
	task_context.cc task_context_params.cc unicodetext.cc utils.cc workspace.cc \
	cld_3/protos/sentence.pb.cc cld_3/protos/feature_extractor.pb.cc cld_3/protos/task_spec.pb.cc \
	-o libcld3.so
	ln -sf $(CLD3_SRC_DIR)/libcld3.so libcld3.so

libced.so:
	cd third-party/compact_enc_det && cmake -DBUILD_SHARED_LIBS=ON . && make ced
	ln -sf third-party/compact_enc_det/lib/libced.so libced.so

CARES_SRC_DIR=third-party/c-ares
libcares.so: libcares.so.2
	ln -sf libcares.so.2 libcares.so

libcares.so.2:
	cd $(CARES_SRC_DIR) && ./buildconf && ./configure && make
	ln -sf $(CARES_SRC_DIR)/.libs/libcares.so.2 libcares.so.2

#always rebuild if needed
.PHONY: libword_variations.a
libword_variations.a:
	$(MAKE) -C word_variations/
	ln -sf word_variations/libword_variations.a libword_variations.a

.PHONY: libsto.a
libsto.a:
	$(MAKE) -C sto/
	ln -sf sto/libsto.a libsto.a

.PHONY: libunicode.a
libunicode.a:
	$(MAKE) -C unicode/
	ln -sf unicode/libunicode.a libunicode.a

.PHONY: libtokenizer.a
libtokenizer.a:
	$(MAKE) -C tokenizer libtokenizer.a
	ln -sf tokenizer/libtokenizer.a libtokenizer.a

wanted_check_api.so: WantedCheckExampleLib.o
	$(CXX) WantedCheckExampleLib.o -shared -o $@
WantedCheckExampleLib.o: WantedCheckExampleLib.cpp
	$(CXX) -fPIC -c WantedCheckExampleLib.cpp

slacktee.sh:
	ln -sf third-party/slacktee/slacktee.sh slacktee.sh 2>/dev/null


Version.o: FORCE
FORCE:


gb: $(LIBFILES) $(OBJS) main.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ main.o $(OBJS) $(LIBS)

GbDns.o: libcares.so.2


.PHONY: static
static: $(LIBFILES) $(OBJS) main.o
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
	$(DIST_LIBFILES) \
	$(DIST_DIR)
	@cp third-party/cld2/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-CLD2
	@cp third-party/cld3/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-CLD3
	@cp third-party/compact_enc_det/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-CED
	@cp third-party/c-ares/LICENSE.md $(DIST_DIR)/LICENSE-3RD-PARTY-CARES
	@cp third-party/slacktee/LICENSE $(DIST_DIR)/LICENSE-3RD-PARTY-SLACKTEE
	@tar -czvf $(DIST_DIR).tar.gz $(DIST_DIR)
	@rm -rf $(DIST_DIR)


# doxygen
doc:
	doxygen doxygen/doxygen_config.conf


# used for tools/unittest
libgb.a: $(OBJS) libsto.a libword_variations.a libunicode.a
	ar rcs $@ $^ word_variations/*.o sto/*.o unicode/*.o tokenizer/*.o

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
	-rm -f *.o *.d gb core core.* libgb.a libsto.a libword_variations.a libunicode.a
	-rm -f gmon.*
	-rm -f *.gcda *.gcno coverage*.html
	-rm -f *.ll *.ll.out pstack.txt
	-rm -f entities.inc
	-rm -f default_css.inc
	-rm -f query_stop_words.??.inc query_stop_words_list.inc
	-rm -f tlds.inc
	$(MAKE) -C test $@
	$(MAKE) -C word_variations/ $@
	$(MAKE) -C sto/ $@
	$(MAKE) -C unicode/ $@
	$(MAKE) -C tokenizer $@


.PHONY: cleandb
cleandb:
	rm -rf coll.main.?
	rm -f *-saved.dat spiderproxystats.dat addsinprogress.dat robots.txt.cache dns.cache rebalance.txt

.PHONY: cleantest
cleantest: cleandb
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

# shortcuts
.PHONY: release-safe
release-safe:
	$(MAKE) config=$@

.PHONY: debug
debug:
	$(MAKE) config=$@

.PHONY: debug-safe
debug-safe:
	$(MAKE) config=$@

.PHONY: debug-test
debug-test:
	$(MAKE) config=$@

.PHONY: sanitize-address
sanitize-address:
	$(MAKE) config=$@

.PHONY: sanitize-undefined
sanitize-undefined:
	$(MAKE) config=$@

.PHONY: sanitize-thread
sanitize-thread:
	$(MAKE) config=$@

.PHONY: sanitize-leak
sanitize-leak:
	$(MAKE) config=$@

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

query_stop_words.xx.inc: query_stop_words.xx.txt generate_query_stop_words.sh
	./generate_query_stop_words.sh xx $< $@
query_stop_words.en.inc: query_stop_words.en.txt generate_query_stop_words.sh
	./generate_query_stop_words.sh en $< $@
query_stop_words.de.inc: query_stop_words.de.txt generate_query_stop_words.sh
	./generate_query_stop_words.sh de $< $@
query_stop_words.da.inc: query_stop_words.da.txt generate_query_stop_words.sh
	./generate_query_stop_words.sh da $< $@
query_stop_words_list.inc: ./generate_query_stop_word_languages.sh query_stop_words.xx.inc query_stop_words.en.inc query_stop_words.de.inc query_stop_words.da.inc
	./generate_query_stop_word_languages.sh $@
StopWords.o: query_stop_words_list.inc

default_css.inc: default.css
	echo "static const char embedded_default_css[] =" >$@.tmp
	sed -e 's/^/"/g' -e 's/$$/\\n"/g' <default.css >>$@.tmp
	echo ";" >>$@.tmp
	mv $@.tmp $@

Pages.o: default_css.inc


Domains.cpp: tlds.inc

tlds.inc: generate_tld_list.sh tlds-alpha-by-domain.txt tlds-official-2nd-level-domains.txt
	./generate_tld_list.sh $@

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
DEPS+=main.d

-include $(DEPS)

