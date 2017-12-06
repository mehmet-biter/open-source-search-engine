CPPFLAGS += -O3
CPPFLAGS += -g
CPPFLAGS += -std=c++11
CPPFLAGS += -Wall

.PHONY: default
default: libsto.a dump_sto

libsto.a: sto.o
	ar rcs $@ $^

sto.o: sto.h sto.cpp

dump_sto: dump_sto.o libsto.a
	$(CXX) $^ -o $@

dump_sto.o: dump_sto.cpp sto.h


.PHONY: clean
clean:
	-rm -f *.o
	-rm -f libsto.a
	-rm -f core
	-rm -f sto.unittest
	-rm -f dump_sto
