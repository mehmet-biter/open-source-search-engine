#!/usr/bin/env python
import os
import json
import urllib2
import sys
from sets import Set


def die(msg):
	print >>sys.stderr,msg
	sys.exit(1)


# 1: Fetch (if not already done) https://www.w3.org/TR/html5-author/entities.json
# 2: Transform into nice 'Entity' data entries for inclusion in Entities.cpp



#if 'entities.json' doesn't exist then fetch it
filename = "entities.json"
if not os.path.exists(filename):
	url = "https://www.w3.org/TR/html5-author/entities.json"
	f = urllib2.urlopen(url)
	
	if f.getcode()!=200:
		die("Could not fetch %s"%url)
	
	r = f.read()
	json_entities = json.loads(r)
	
	with open(filename,"w") as out_file:
		out_file.write(r)
else:
	#load existing file
	with open(filename,"r") as in_file:
		json_entities = json.loads(in_file.read())


max_entity_name_len = 0

#keep track of which entities we have seen. The w3c list contains duplicates.
seen_entitites = Set()

print "static struct Entity s_entities[] = {"
for entity_name,data in json_entities.iteritems():
	if entity_name[0]!='&':
		die("entity %s does not start with an ampersand"%entity_name)
	entity_name = entity_name[1:]
	
	if entity_name[-1]==';':
		#strip off if present (w3c file is inconsistent)
		entity_name=entity_name[0:-1]
	
	if entity_name in seen_entitites:
		continue
	seen_entitites.add(entity_name)
	codepoints = data[u'codepoints']
	if len(codepoints)<1 or len(codepoints)>2:
		die("Unexpected codepoint count for entity %s",entity_name)
	codepoint_count = len(codepoints)
	if len(codepoints)<2:
		codepoints.append(0) #make codepoints a full array so compilers/flexelint dont complain about too few initializers
	
	max_entity_name_len = max(max_entity_name_len,len(entity_name))
	
	print '	{"&%s",	%d, {%s},	0, ""},'%(entity_name, codepoint_count, ",".join([str(c) for c in codepoints]))
print "};"

print "static const int max_entity_name_len = %d;"%(max_entity_name_len+1)
