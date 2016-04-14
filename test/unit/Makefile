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
	XmlTest.o

.PHONY: all
all: $(TARGET)

.PHONY: libgb.a
libgb.a:
	make -C $(BASE_DIR) libgb.a

ucdata:
	ln -s $(BASE_DIR)/$@ .

$(BASE_DIR)/libcld2_full.so:
	make -C $(BASE_DIR) libcld2_full.so

libgtest.so:
	cd $(GTEST_DIR) && cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Debug && make
	ln -s $(GTEST_DIR)/$@ .

CPPFLAGS += -g
CPPFLAGS += -Wno-write-strings
CPPFLAGS += -Wl,-rpath=. -Wl,-rpath=$(BASE_DIR)
CPPFLAGS += -I$(BASE_DIR) -I$(GTEST_DIR)/include

LIBS += -L./ -lgtest 
LIBS += $(BASE_DIR)/libgb.a $(BASE_DIR)/libiconv64.a -lz -lpthread -lssl -lcrypto
LIBS += -L$(BASE_DIR) -lcld2_full

$(TARGET): libgtest.so libgb.a $(BASE_DIR)/libcld2_full.so $(OBJECTS)
	$(CXX) $(CPPFLAGS) $(OBJECTS) $(LIBS) -o $@

.PHONY: check
check: TARGET_PRE="valgrind"
check: test

.PHONY: test
test: all ucdata
	$(TARGET_PRE) ./$(TARGET) $(TEST_ARGS)

.PHONY: clean
clean:
	rm -f *.o $(TARGET) core.*
