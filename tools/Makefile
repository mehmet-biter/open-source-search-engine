.DEFAULT_GOAL := all

TARGETS := $(sort $(patsubst %.cpp, %, $(wildcard *.cpp)))

BASE_DIR ?= ..

.PHONY: all
all: $(TARGETS)

.PHONY: libgb.a
libgb.a:
	$(MAKE) -C $(BASE_DIR) libgb.a

ucdata:
	ln -s $(BASE_DIR)/$@ .

$(BASE_DIR)/libcld2_full.so:
	$(MAKE) -C $(BASE_DIR) libcld2_full.so

CPPFLAGS += -g
CPPFLAGS += -Wall -Wno-write-strings
CPPFLAGS += -Wl,-rpath=. -Wl,-rpath=$(BASE_DIR)
CPPFLAGS += -I$(BASE_DIR)
CPPFLAGS += -std=c++11

# exported in parent make
CPPFLAGS += $(CONFIG_CPPFLAGS)

LIBS += $(BASE_DIR)/libgb.a -lz -lpthread -lssl -lcrypto -lpcre
LIBS += -L$(BASE_DIR) -lcld2_full

%: libgb.a $(BASE_DIR)/libcld2_full.so %.cpp
	$(CXX) $(CPPFLAGS) $@.cpp $(LIBS) -o $@

.PHONY: clean
clean:
	rm -f *.o $(TARGETS) core.*
	rm -f *.gcda *.gcno

