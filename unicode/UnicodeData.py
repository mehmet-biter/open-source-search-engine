#!/usr/bin/python
import sys
reload(sys)
sys.setdefaultencoding("utf_8")

#Information about a single codepoint. we only keep what we need (eg. we don't care about bidi class)
class CodePointInfo:
	def __init__(self):
		self.codepoint = None
		self.name = None
		self.canonical_combining_class = None
		self.general_category = None
		self.decomposition_type = None
		self.decomposition = None
		self.simple_uppercase_mapping = None
		self.simple_titlecase_mapping = None
		self.simple_lowercase_mapping = None
		self.props = set()
		self.derived_core_props = set()
		self.script_name = None
		self.case_folding_status = None #C/F/S/T
		self.case_folding = None
		self.uppercase_folding = None
		self.lowercase_folding = None
		self.titlecase_folding = None
	def __str__(self):
		return "CodePointInfo(codepoint=%04X, name='%s', general_category=%s, props=%s, derived_core_props=%s)"%(self.codepoint,self.name,self.general_category,self.props,self.derived_core_props)
data = {}


"""Read in UnicodeData.txt and PropList.txt"""
#example line:
#00F9;LATIN SMALL LETTER U WITH GRAVE;Ll;0;L;0075 0300;;;;N;LATIN SMALL LETTER U GRAVE;;00D9;;00D9
#codepoint
#     name
#                                     general category
#                                        canonical_combining_class
#                                          bidi class
#                                            decomposition mapping
#                                                      numeric type
#                                                       numeric value 1
#                                                        numeric value 2
#                                                         bidi mirrored
#                                                           unicode 1 name
#                                                                                      ISO comment
#                                                                                       simple uppercase mapping
#                                                                                            simple titlecase mapping
#                                                                                             simple lowercase mapping
#example line from PropList.txt:
#2010..2011    ; Hyphen # Pd   [2] HYPHEN..NON-BREAKING HYPHEN
def read(dir):
	with open(dir+"/UnicodeData.txt") as f:
		for line in f.readlines():
			s = line.strip().split(';')
			cpi = CodePointInfo()
			codepoint = int(s[0],16)
			cpi.codepoint = codepoint
			cpi.name = s[1]
			cpi.general_category = s[2]
			if len(s[5])>0:
				tmp = s[5]
				if tmp[0]=='<': #decomposition type in angle brackets
					cpi.decomposition_type = tmp[1:].split('>')[0]
					tmp = tmp.split('>')[1]
				tmp = tmp.strip()
				cpi.decomposition = []
				for decomposition_codepoint_str in tmp.split(' '):
					decomposition_codepoint = int(decomposition_codepoint_str,16)
					cpi.decomposition.append(decomposition_codepoint)
			cpi.simple_uppercase_mapping = int(s[12],16) if len(s[12])>0 else None
			cpi.simple_lowercase_mapping = int(s[13],16) if len(s[13])>0 else None
			cpi.simple_titlecase_mapping = int(s[14],16) if len(s[14])>0 else None
			cpi.uppercase_folding = [cpi.simple_uppercase_mapping] if cpi.simple_uppercase_mapping else None
			cpi.lowercase_folding = [cpi.simple_lowercase_mapping] if cpi.simple_lowercase_mapping else None
			cpi.titlecase_folding = [cpi.simple_titlecase_mapping] if cpi.simple_titlecase_mapping else None
			data[codepoint] = cpi
	
	with open(dir+"/PropList.txt") as f:
		for line in f.readlines():
			if line[0]=='#':
				continue
			if len(line)<4:
				continue
			s = line.split(';')
			prop_name = s[1].strip(' ').split('#')[0].strip(' ')
			if ".." in s[0]:
				first_codepoint = int(s[0].split('.')[0],16)
				last_codepoint = int(s[0].split('.')[2],16)
				for codepoint in range(first_codepoint,last_codepoint+1):
					if codepoint in data:
						data[codepoint].props.add(prop_name)
			else:
				codepoint = int(s[0],16)
				if codepoint in data:
					data[codepoint].props.add(prop_name)
	
	with open(dir+"/DerivedCoreProperties.txt") as f:
		for line in f.readlines():
			if line[0]=='#':
				continue
			if len(line)<4:
				continue
			s = line.split(';')
			prop_name = s[1].strip(' ').split('#')[0].strip(' ')
			if ".." in s[0]:
				first_codepoint = int(s[0].split('.')[0],16)
				last_codepoint = int(s[0].split('.')[2],16)
				for codepoint in range(first_codepoint,last_codepoint+1):
					if codepoint in data:
						data[codepoint].derived_core_props.add(prop_name)
			else:
				codepoint = int(s[0],16)
				if codepoint in data:
					data[codepoint].derived_core_props.add(prop_name)
	
	with open(dir+"/Scripts.txt") as f:
		for line in f.readlines():
			if line[0]=='#':
				continue
			if len(line)<4:
				continue
			s = line.split('#')[0].split(';')
			script_name = s[1].strip(' ')
			if ".." in s[0]:
				first_codepoint = int(s[0].split('.')[0],16)
				last_codepoint = int(s[0].split('.')[2],16)
				for codepoint in range(first_codepoint,last_codepoint+1):
					if codepoint in data:
						data[codepoint].script_name = script_name
			else:
				codepoint = int(s[0],16)
				if codepoint in data:
					data[codepoint].script_name = script_name


	with open(dir+"/CaseFolding.txt") as f:
		for line in f.readlines():
			if line[0]=='#':
				continue
			if len(line)<4:
				continue
			s = line.split('#')[0].split(';')
			codepoint = int(s[0],16)
			case_folding_status = s[1]
			if case_folding_status=="C":
				if codepoint in data:
					cpi = data[codepoint]
					#establish simple case mappings if present
					if cpi.simple_lowercase_mapping:
						cpi.lowercase_folding = [cpi.simple_lowercase_mapping]
					if cpi.simple_uppercase_mapping:
						cpi.uppercase_folding = [cpi.simple_uppercase_mapping]
					if cpi.simple_titlecase_mapping:
						cpi.titlecase_folding = [cpi.simple_titlecase_mapping]

					cpi.case_folding_status = s[1]
					case_folding=[]
					for x in s[2].strip(' ').split(' '):
						case_folding.append(int(x,16))
					#print "codepoint=%04X case_folding=%s"%(cpi.codepoint,case_folding)
					if cpi.general_category=='Lu': #is uppercase. how to fold it to lowercase...
						cpi.lowercase_folding = case_folding
					elif cpi.general_category=='Ll': #is titlecase. how to fold it to lowercase...
						cpi.uppercase_folding = case_folding
					elif cpi.general_category=='Lt': #is lowercase. how to fold it to uppercase...
						cpi.uppercase_folding = case_folding
			else:
				#there seem to be something fishy about the non-'C' status entries
				#The 'T' status is for turkish i and i-dotless
				#The 'S' is simple and equivalent to the data from UnicodeData.txt
				#The 'F' entries seem to also map lowercase to lowercase. So to map to uppercase we have to use the casefolding to get to the lowercase alphabetic codepoints, do uppercase on those , and the try to find a match in the decomposition tables.
				#but currently we don't care about uppercasing so we just ignore that problem for now.
				pass
				

if __name__ == "__main__":
	read("/root/dl/ucd")
	print "codepoints:", len(data)
	
	c0=0
	c1=0
	for cpi in data.itervalues():
		if len(cpi.props)>=1:
			c0+=1
		if len(cpi.props)>=2:
			c1+=1
	print "codepoints with properties:", c0
	print "codepoints with 2+ properties:", c1
	c = {}
	for cpi in data.itervalues():
		for p in cpi.props:
			if p in c:
				c[p] = c[p] +1
			else:
				c[p] = 1
	print "Property breakdown:"
	for (k,v) in c.iteritems():
		print "\t%-40s\t%d"%(k,v)
	
	c0=0
	c1=0
	for cpi in data.itervalues():
		if len(cpi.derived_core_props)>=1:
			c0+=1
		if len(cpi.derived_core_props)>=2:
			c1+=1
	print "codepoints with Derived-Core-properties:", c0
	print "codepoints with 2+ Derived-Core-properties:", c1
	c = {}
	for cpi in data.itervalues():
		for p in cpi.derived_core_props:
			if p in c:
				c[p] = c[p] +1
			else:
				c[p] = 1
	print "Derived-Core-Property breakdown:"
	for (k,v) in c.iteritems():
		print "\t%-40s\t%d"%(k,v)
	
	c0=0
	for cpi in data.itervalues():
		if cpi.decomposition and len(cpi.decomposition)>0:
			c0+=1
	print "codepoints with decomposition:", c0

	c0=0
	for cpi in data.itervalues():
		if cpi.script_name:
			c0+=1
	print "codepoints with script name:", c0

	c0=0
	c1=0
	for cpi in data.itervalues():
		if cpi.lowercase_folding:
			c0+=1
		if cpi.uppercase_folding:
			c1+=1
	print "codepoints with lowercase folding: %d"%c0
	print "codepoints with uppercase folding: %d"%c1
	
#	#dump upper/lowercase mappings
#	for cpi in data.itervalues():
#		if cpi.lowercase_folding:
#			if len(cpi.lowercase_folding)==1:
#				print "%04X\tlower(%s)\t%s"%(cpi.codepoint,unichr(cpi.codepoint),unichr(cpi.lowercase_folding[0]))
#			else:
#				print "%04X\tlower(%s)\t%s"%(cpi.codepoint,unichr(cpi.codepoint),cpi.lowercase_folding)
#		if cpi.uppercase_folding:
#			if len(cpi.uppercase_folding)==1:
#				print "%04X\tupper(%s)\t%s"%(cpi.codepoint,unichr(cpi.codepoint),unichr(cpi.uppercase_folding[0]))
#			else:
#				print "%04X\tupper(%s)\t%s"%(cpi.codepoint,unichr(cpi.codepoint),cpi.uppercase_folding)
