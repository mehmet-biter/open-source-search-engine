#UCDDIR must point to where the data files from unicode.org are.
UCDDIR?=.
CPPFLAGS += -O3 -g
CPPFLAGS += -std=c++11
CPPFLAGS += -Wall

.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp

.PHONY: all
all: libunicode.a \
     unicode_scripts.dat \
     unicode_general_categories.dat \
     unicode_properties.dat \
     unicode_wordchars.dat \
     unicode_to_lowercase.dat \
     unicode_canonical_decomposition.dat


libunicode.a: UCMap.o UCMaps.o
	ar rcs $@ UCMap.o UCMaps.o

UCMap.o: UCMap.h UCMap.cpp
UCMap_unittest.o: UCMap.h UCMap_unittest.cpp
UCMap_unittest: UCMap_unittest.o UCMap.o
	g++ -g	UCMap_unittest.o UCMap.o -o $@

.PHONY: UCMap_unittest_run
UCMap_unittest_run: UCMap_unittest
	./UCMap_unittest


UCMaps.o: UCMaps.h UCMaps.cpp UCMap.h


unicode_scripts.dat unicode_general_categories.dat unicode_properties.dat unicode_wordchars.dat unicode_to_lowercase.dat unicode_canonical_decomposition.dat: generate_tables.py
	./generate_tables.py $(UCDDIR)


.PHONY: clean
clean:
	rm -f *.o *.a UCMap_unittest

.PHONY: mrproper
mrproper: clean
	rm -f *.dat
