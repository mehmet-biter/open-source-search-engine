#UCDDIR must point to where the data files from unicode.org are.
UCDDIR?=.
CPPFLAGS += -O3 -g
CPPFLAGS += -std=c++11
CPPFLAGS += -Wall

.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp

DATFILES=unicode_scripts.dat unicode_general_categories.dat unicode_properties.dat unicode_wordchars.dat unicode_to_lowercase.dat unicode_canonical_decomposition.dat

ALL=libunicode.a
ifneq (,$(wildcard $(UCDDIR)/UnicodeData.txt))
ALL+=$(DATFILES)
endif
.PHONY: all
all: $(ALL)


libunicode.a: UCMap.o UCMaps.o UCDecompose.o
	ar rcs $@ UCMap.o UCMaps.o

UCMap.o: UCMap.h UCMap.cpp
UCMap_unittest.o: UCMap.h UCMap_unittest.cpp
UCMap_unittest: UCMap_unittest.o UCMap.o
	g++ -g	UCMap_unittest.o UCMap.o -o $@

.PHONY: UCMap_unittest_run
UCMap_unittest_run: UCMap_unittest
	./UCMap_unittest


UCMaps.o: UCMaps.h UCMaps.cpp UCMap.h


UCDecompose.o: UCDecompose.h UCDecompose.cpp
UCDecompose_unittest.o: UCMaps.h UCMap.h UCDecompose_unittest.cpp
UCDecompose_unittest: UCDecompose_unittest.o UCDecompose.o UCMap.o UCMaps.o
	g++ -g	UCDecompose_unittest.o UCDecompose.o UCMap.o UCMaps.o -o $@

.PHONY: UCDecompose_unittest_run
UCDecompose_unittest_run: UCDecompose_unittest $(DATFILES)
	./UCDecompose_unittest


.PHONY: datfiles
datfiles: $(DATFILES)
$(DATFILES): generate_tables.py
	./generate_tables.py $(UCDDIR)


.PHONY: clean
clean:
	rm -f *.o *.a UCMap_unittest UCDecompose_unittest

.PHONY: mrproper
mrproper: clean
	rm -f *.dat
