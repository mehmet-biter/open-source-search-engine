CPPFLAGS += -O3
CPPFLAGS += -std=c++11
CPPFLAGS += -Wall
CPPFLAGS+= -I..


.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp


OBJS=WordVariations.o \
     WordVariationsGenerator_danish.o \
     STOWordVariationGenerator.o \


.PHONY: default
default: libword_variations.a

libword_variations.a: $(OBJS)
	ar rcs $@ $^

.PHONY: clean
clean:
	-rm -f *.o
	-rm -f libword_variations.a
	-rm -f core
