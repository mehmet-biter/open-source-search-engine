CPPFLAGS += -O3 -g
CPPFLAGS += -std=c++11
CPPFLAGS += -Wall
CPPFLAGS += -I.. -I../unicode

.cpp.o:
	$(CXX) $(DEFS) $(CPPFLAGS) -c $*.cpp


.PHONY: default
default: run


tokenizer_unittest: tokenizer.o tokenizer2.o tokenizer_unittest.o
	g++ -g tokenizer.o tokenizer2.o tokenizer_unittest.o ../unicode/libunicode.a ../utf8_fast.o ../utf8.o ../EGStack.o -o $@

PHONY: run
run: tokenizer_unittest
	LD_LIBRARY_PATH=.. ./tokenizer_unittest
