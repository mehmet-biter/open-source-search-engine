.DEFAULT_GOAL := all

GTEST_DIR = googletest/googletest
BASE_DIR ?= ../..

TARGET = GigablastTest
OBJECTS = UrlTest.o

.PHONY: all
all: $(TARGET)

.PHONY: libgb.a
libgb.a:
	make -C $(BASE_DIR) libgb.a

$(BASE_DIR)/libcld2_full.so:
	make -C $(BASE_DIR) libcld2_full.so

libgtest.so:
	cd $(GTEST_DIR) && cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Debug && make
	ln -s $(GTEST_DIR)/$@ .

CPPFLAGS += -ggdb
CPPFLAGS += -Wl,-rpath=. -Wl,-rpath=$(BASE_DIR)
CPPFLAGS += -I$(BASE_DIR) -I$(GTEST_DIR)/include

LIBS += -L./ -lgtest 
LIBS += $(BASE_DIR)/libgb.a $(BASE_DIR)/libiconv64.a -lpthread -lssl -lcrypto -lz
LIBS += -L$(BASE_DIR) -lcld2_full

$(TARGET): libgtest.so libgb.a $(BASE_DIR)/libcld2_full.so $(OBJECTS)
	$(CXX) $(CPPFLAGS) $(OBJECTS) $(LIBS) -o $@

.PHONY: test
test: all
	./$(TARGET)

.PHONY: clean
clean:
	rm -f *.o $(TARGET)
