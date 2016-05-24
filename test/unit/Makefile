.DEFAULT_GOAL := test

GTEST_DIR = googletest/googletest
BASE_DIR ?= ../..

TARGET = GigablastTest
OBJECTS = GigablastTest.o \
	BitOperationsTest.o \
	FctypesTest.o \
	JsonTest.o \
	PosTest.o ProcessTest.o \
	RobotRuleTest.o RobotsTest.o \
	ScalingFunctionsTest.o SummaryTest.o \
	UnicodeTest.o UrlTest.o \
	WordsTest.o \
	XmlTest.o \

.PHONY: all
all: $(TARGET)

.PHONY: libgb.a
libgb.a:
	$(MAKE) -C $(BASE_DIR) libgb.a

ucdata:
	ln -s $(BASE_DIR)/$@ .

$(BASE_DIR)/libcld2_full.so:
	$(MAKE) -C $(BASE_DIR) libcld2_full.so

libgtest.so:
	cd $(GTEST_DIR) && cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Debug && make
	ln -s $(GTEST_DIR)/$@ .

CPPFLAGS += -g
CPPFLAGS += -Wno-write-strings
CPPFLAGS += -Wl,-rpath=. -Wl,-rpath=$(BASE_DIR)
CPPFLAGS += -I$(BASE_DIR) -I$(GTEST_DIR)/include
CPPFLAGS += -std=c++11

# exported in parent make
CPPFLAGS += $(CONFIG_CPPFLAGS)

LIBS += -L./ -lgtest 
LIBS += $(BASE_DIR)/libgb.a -lz -lpthread -lssl -lcrypto
LIBS += -L$(BASE_DIR) -lcld2_full

$(TARGET): libgtest.so libgb.a $(BASE_DIR)/libcld2_full.so $(OBJECTS)
	$(CXX) $(CPPFLAGS) $(OBJECTS) $(LIBS) -o $@

.PHONY: check
check: TARGET_PRE="valgrind"
check: test

.PHONY: test
test: all ucdata
	-$(TARGET_PRE) ./$(TARGET) $(TEST_ARGS)

.PHONY: clean
clean:
	rm -f *.o $(TARGET) core.*
	rm -f *.gcda *.gcno


JobSchedulerTest00: JobSchedulerTest00.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest00.o $(LIBS) -o $@
.PHONY: JobSchedulerTest00_run
JobSchedulerTest00_run: JobSchedulerTest00
	./JobSchedulerTest00
JobSchedulerTest01: JobSchedulerTest01.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest01.o $(LIBS) -o $@
.PHONY: JobSchedulerTest01_run
JobSchedulerTest01_run: JobSchedulerTest01
	./JobSchedulerTest01
JobSchedulerTest02: JobSchedulerTest02.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest02.o $(LIBS) -o $@
.PHONY: JobSchedulerTest02_run
JobSchedulerTest02_run: JobSchedulerTest02
	./JobSchedulerTest02
JobSchedulerTest03: JobSchedulerTest03.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest03.o $(LIBS) -o $@
.PHONY: JobSchedulerTest03_run
JobSchedulerTest03_run: JobSchedulerTest03
	./JobSchedulerTest03
JobSchedulerTest04: JobSchedulerTest04.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest04.o $(LIBS) -o $@
.PHONY: JobSchedulerTest04_run
JobSchedulerTest04_run: JobSchedulerTest04
	./JobSchedulerTest04
JobSchedulerTest05: JobSchedulerTest05.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest05.o $(LIBS) -o $@
.PHONY: JobSchedulerTest05_run
JobSchedulerTest05_run: JobSchedulerTest05
	./JobSchedulerTest05
JobSchedulerTest06: JobSchedulerTest06.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest06.o $(LIBS) -o $@
.PHONY: JobSchedulerTest06_run
JobSchedulerTest06_run: JobSchedulerTest06
	./JobSchedulerTest06
JobSchedulerTest07: JobSchedulerTest07.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest07.o $(LIBS) -o $@
.PHONY: JobSchedulerTest07_run
JobSchedulerTest07_run: JobSchedulerTest07
	./JobSchedulerTest07
JobSchedulerTest08: JobSchedulerTest08.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest08.o $(LIBS) -o $@
.PHONY: JobSchedulerTest08_run
JobSchedulerTest08_run: JobSchedulerTest08
	./JobSchedulerTest08
JobSchedulerTest09: JobSchedulerTest09.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest09.o $(LIBS) -o $@
.PHONY: JobSchedulerTest09_run
JobSchedulerTest09_run: JobSchedulerTest09
	./JobSchedulerTest09
JobSchedulerTest10: JobSchedulerTest10.o libgb.a GigablastTest.o
	$(CXX) $(CPPFLAGS) JobSchedulerTest10.o $(LIBS) -o $@
.PHONY: JobSchedulerTest10_run
JobSchedulerTest10_run: JobSchedulerTest10
	./JobSchedulerTest10
