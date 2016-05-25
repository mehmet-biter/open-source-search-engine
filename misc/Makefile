
test_parser: $(OBJS) test_parser.o Makefile
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ test_parser.o $(OBJS) $(LIBS)
test_parser2: $(OBJS) test_parser2.o Makefile
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ test_parser2.o $(OBJS) $(LIBS)

test_hash: test_hash.o $(OBJS)
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ test_hash.o $(OBJS) $(LIBS)
test_norm: $(OBJS) test_norm.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ test_norm.o $(OBJS) $(LIBS)
test_convert: $(OBJS) test_convert.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ test_convert.o $(OBJS) $(LIBS)

supported_charsets: $(OBJS) supported_charsets.o supported_charsets.txt
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ supported_charsets.o $(OBJS) $(LIBS)
create_ucd_tables: $(OBJS) create_ucd_tables.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ create_ucd_tables.o $(OBJS) $(LIBS)

blaster2: $(OBJS) blaster2.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
udptest: $(OBJS) udptest.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
dnstest: $(OBJS) dnstest.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
memtest: memtest.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o
hashtest: hashtest.cpp
	$(CXX) $(O3) -o hashtest hashtest.cpp
mergetest: $(OBJS) mergetest.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
treetest: $(OBJ) treetest.o
	$(CXX) $(DEFS) $(O2) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
nicetest: nicetest.o
	$(CXX) -o nicetest nicetest.cpp


monitor: $(OBJS) monitor.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ monitor.o $(OBJS) $(LIBS)
reindex: $(OBJS) reindex.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
urlinfo: $(OBJS) urlinfo.o
	$(CXX) $(DEFS) $(CPPFLAGS) -o $@ $(OBJS) urlinfo.o $(LIBS)

