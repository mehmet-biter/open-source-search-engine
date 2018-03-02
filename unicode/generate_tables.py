#!/usr/bin/python
# -*- coding: utf-8 -*-
import sys
import UnicodeData
import struct

if len(sys.argv)!=2:
	print >>sys.stderr, "Usage: %s <unicode_data_dir>"%sys.argv[0]
	print >>sys.stderr, "Takes UnicodeData.txt, PropList.txt ... and turns into optimized table files"
	sys.exit(99)

unicode_data_dir = sys.argv[1]
UnicodeData.read(unicode_data_dir)

last_codepoint = max(UnicodeData.data.keys())

print "Last codepoint: %d"%last_codepoint

def is_interesting(codepoint):
	return codepoint in [0x002D, 0x00AD, 0x2010, 0x2011]

## Generate codepoint->script mapping
script_name_to_code_mapping = {
	"Adlam":1,
	"Ahom":2,
	"Anatolian_Hieroglyphs":3,
	"Arabic":4,
	"Armenian":5,
	"Avestan":6,
	"Balinese":7,
	"Bamum":8,
	"Bassa_Vah":9,
	"Batak":10,
	"Bengali":11,
	"Bhaiksuki":12,
	"Bopomofo":13,
	"Brahmi":14,
	"Braille":15,
	"Buginese":16,
	"Buhid":17,
	"Canadian_Aboriginal":18,
	"Carian":19,
	"Caucasian_Albanian":20,
	"Chakma":21,
	"Cham":22,
	"Cherokee":23,
	"Common":24,
	"Coptic":25,
	"Cuneiform":26,
	"Cypriot":27,
	"Cyrillic":28,
	"Deseret":29,
	"Devanagari":30,
	"Duployan":31,
	"Egyptian_Hieroglyphs":32,
	"Elbasan":33,
	"Ethiopic":34,
	"Georgian":35,
	"Glagolitic":36,
	"Gothic":37,
	"Grantha":38,
	"Greek":39,
	"Gujarati":40,
	"Gurmukhi":41,
	"Han":42,
	"Hangul":43,
	"Hanunoo":44,
	"Hatran":45,
	"Hebrew":46,
	"Hiragana":47,
	"Imperial_Aramaic":48,
	"Inherited":49,
	"Inscriptional_Pahlavi":50,
	"Inscriptional_Parthian":51,
	"Javanese":52,
	"Kaithi":53,
	"Kannada":54,
	"Katakana":55,
	"Kayah_Li":56,
	"Kharoshthi":57,
	"Khmer":58,
	"Khojki":59,
	"Khudawadi":60,
	"Lao":61,
	"Latin":62,
	"Lepcha":63,
	"Limbu":64,
	"Linear_A":65,
	"Linear_B":66,
	"Lisu":67,
	"Lycian":68,
	"Lydian":69,
	"Mahajani":70,
	"Malayalam":71,
	"Mandaic":72,
	"Manichaean":73,
	"Marchen":74,
	"Masaram_Gondi":75,
	"Meetei_Mayek":76,
	"Mende_Kikakui":77,
	"Meroitic_Cursive":78,
	"Meroitic_Hieroglyphs":79,
	"Miao":80,
	"Modi":81,
	"Mongolian":82,
	"Mro":83,
	"Multani":84,
	"Myanmar":85,
	"Nabataean":86,
	"New_Tai_Lue":87,
	"Newa":88,
	"Nko":89,
	"Nushu":90,
	"Ogham":91,
	"Ol_Chiki":92,
	"Old_Hungarian":93,
	"Old_Italic":94,
	"Old_North_Arabian":95,
	"Old_Permic":96,
	"Old_Persian":97,
	"Old_South_Arabian":98,
	"Old_Turkic":99,
	"Oriya":100,
	"Osage":101,
	"Osmanya":102,
	"Pahawh_Hmong":103,
	"Palmyrene":104,
	"Pau_Cin_Hau":105,
	"Phags_Pa":106,
	"Phoenician":107,
	"Psalter_Pahlavi":108,
	"Rejang":109,
	"Runic":110,
	"Samaritan":111,
	"Saurashtra":112,
	"Sharada":113,
	"Shavian":114,
	"Siddham":115,
	"SignWriting":116,
	"Sinhala":117,
	"Sora_Sompeng":118,
	"Soyombo":119,
	"Sundanese":120,
	"Syloti_Nagri":121,
	"Syriac":122,
	"Tagalog":123,
	"Tagbanwa":124,
	"Tai_Le":125,
	"Tai_Tham":126,
	"Tai_Viet":127,
	"Takri":128,
	"Tamil":129,
	"Tangut":130,
	"Telugu":131,
	"Thaana":132,
	"Thai":133,
	"Tibetan":134,
	"Tifinagh":135,
	"Tirhuta":136,
	"Ugaritic":137,
	"Unknown":138,
	"Vai":139,
	"Warang_Citi":140,
	"Yi":141,
	"Zanabazar_Square":142
}

#Full map
print "Generating unicode_scripts.dat"
with open("unicode_scripts.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if cpi.script_name:
				script_number = script_name_to_code_mapping[cpi.script_name]
				f.write(chr(script_number))
			else:
				f.write('\0') #codepoint with no script. Usually private-use planes
		else:
			f.write('\0') #missing codepoint
print "Done."


#Unicode 10.0.0 uses 34 distinct properties, so that doesn't readily fit in 32-bit fields.
#we deliberaty leave out the properties "Deprecated" and "Pattern_Syntax", so it fits into 32 bit
property_to_bit_mapping = {
	"White_Space":1<<0,
	"ASCII_Hex_Digit":1<<1,
	"Bidi_Control":1<<2,
	"Dash":1<<3,
	"Diacritic":1<<4,
	"Extender":1<<5,
	"Hex_Digit":1<<6,
	"Hyphen":1<<7,
	"IDS_Binary_Operator":1<<8,
	"IDS_Trinary_Operator":1<<9,
	"Ideographic":1<<10,
	"Join_Control":1<<11,
	"Logical_Order_Exception":1<<12,
	"Noncharacter_Code_Point":1<<13,
	"Other_Alphabetic":1<<14,
	"Other_Default_Ignorable_Code_Point":1<<15,
	"Other_Grapheme_Extend":1<<16,
	"Other_ID_Continue":1<<17,
	"Other_ID_Start":1<<18,
	"Other_Lowercase":1<<19,
	"Other_Math":1<<20,
	"Other_Uppercase":1<<21,
	"Pattern_White_Space":1<<22,
	"Prepended_Concatenation_Mark":1<<23,
	"Quotation_Mark":1<<24,
	"Radical":1<<25,
	"Regional_Indicator":1<<26,
	"Sentence_Terminal":1<<27,
	"Soft_Dotted":1<<28,
	"Terminal_Punctuation":1<<29,
	"Unified_Ideograph":1<<30,
	"Variation_Selector":1<<31,
}

print "Generating unicode_properties.dat"
with open("unicode_properties.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if is_interesting(codepoint): print "U+%04X: props: %s"%(codepoint,cpi.props)
			prop_bits = 0
			for p in cpi.props:
				if p in property_to_bit_mapping:
					prop_bits += property_to_bit_mapping[p]
			f.write(struct.pack("@I",prop_bits))
		else:
			f.write('\0\0\0\0') #missing codepoint
print "Done."


general_category_to_code_mapping = {
	"Cc":1,
	"Cf":2,
	"Co":3,
	"Cs":4,
	"Ll":5,
	"Lm":6,
	"Lo":7,
	"Lt":8,
	"Lu":9,
	"Mc":10,
	"Me":11,
	"Mn":12,
	"Nd":13,
	"Nl":14,
	"No":15,
	"Pc":16,
	"Pd":17,
	"Pe":18,
	"Pf":19,
	"Pi":20,
	"Po":21,
	"Ps":22,
	"Sc":23,
	"Sk":24,
	"Sm":25,
	"So":26,
	"Zl":27,
	"Zp":28,
	"Zs":29
}

print "Generating unicode_general_categories.dat"
with open("unicode_general_categories.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			code = general_category_to_code_mapping[cpi.general_category]
			f.write(chr(code))
		else:
			f.write('\0') #missing codepoint
print "Done."


#What do we consider a word-character wrt. tokenization?
#  latin a-z obviously. And the pendants in greek and cyrillic and other scripts
#  digits 0-9 too
#  but what "superscript two", "circled digit three" ... ?
#Not so easy. We chose to use anything that is marked as alphabetic or digits
#and combining diacritics in case we enounter a decomposed codepoint
#It happens that alphabetic+grapheme-extend+number covers it all
#Except underscore (makes sense in some identifiers) and middle-dot (what?).
#Non-decimal numbers are up for debate, should the codepoints 216B (roman numeral twelve)
#or 0BF1 (tamil number one hundred) be included? Probably. If it turns out that an
#ancient Roman shop was called "Ⅶ-Undecim" then that will be treated as a word string
print "Generating unicode_wordchars.dat"
with open("unicode_wordchars.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		is_wordchar = False
		if codepoint==0x005F or codepoint==0x00B7: #underscore and middle-dot are special
			is_wordchar = False
		elif codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if "Alphabetic" in cpi.derived_core_props or \
			   "Grapheme_Extend" in cpi.derived_core_props:
				is_wordchar = True
			elif cpi.general_category=='Nd' or \
			     cpi.general_category=='Nl' or \
			     cpi.general_category=='No':
				is_wordchar = True
			else:
				is_wordchar = False
		else:
			is_wordchar = False #missing codepoint
		if is_interesting(codepoint): print "U+%04X: '%s' : wordchar=%s"%(codepoint,unichr(codepoint),is_wordchar)
		if is_wordchar:
			f.write('\1')
		else:
			f.write('\0')
print "Done."


#ignorable codepoints. used in conjunction with is_alfanum and script checks. If a codepoint is ignoreable then it can be skipped or included or whatever. It doesn't matter.
print "Generating unicode_is_ignorable.dat"
with open("unicode_is_ignorable.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		is_ignorable = False
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if "Default_Ignorable_Code_Point" in cpi.derived_core_props:
				is_ignorable = True
			else:
				is_ignorable = False
		else:
			is_ignorable = False #missing codepoint
		if is_interesting(codepoint): print "U+%04X: '%s' : is_ignorable=%s"%(codepoint,unichr(codepoint),is_ignorable)
		if is_ignorable:
			f.write('\1')
		else:
			f.write('\0')
				
print "Done"

print "Generating unicode_is_alphabetic.dat"
with open("unicode_is_alphabetic.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if "Alphabetic" in cpi.derived_core_props:
				f.write('\1')
			else:
				f.write('\0')
		else:
			f.write('\0') #missing codepoint
print "Done."

print "Generating unicode_is_uppercase.dat"
with open("unicode_is_uppercase.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if "Uppercase" in cpi.derived_core_props:
				f.write('\1')
			else:
				f.write('\0')
		else:
			f.write('\0') #missing codepoint
print "Done."

print "Generating unicode_is_lowercase.dat"
with open("unicode_is_lowercase.dat","w") as f:
	for codepoint in range(0,last_codepoint+1):
		if codepoint in UnicodeData.data:
			cpi = UnicodeData.data[codepoint]
			if "Lowercase" in cpi.derived_core_props:
				f.write('\1')
			else:
				f.write('\0')
		else:
			f.write('\0') #missing codepoint
print "Done."

#Uppercase/lowercase mapping
#This is not a 1:1 mapping (ref: german sharp s, ligatures, etc).
#and it is locale-specific in some cases. We'll ignore that for now (Turkish i, Greek iota, Lithuanian, i, ...)
#Since it only applies to alphabetic codepoints and only some scripts we make a sparse map
print "Generating unicode_to_uppercase.dat"
with open("unicode_to_uppercase.dat","w") as f:
	for codepoint,cpi in sorted(UnicodeData.data.iteritems()):
		if cpi.uppercase_folding:
			f.write(struct.pack("@I",codepoint))
			f.write(struct.pack("@I",len(cpi.uppercase_folding)))
			for c in cpi.uppercase_folding:
				f.write(struct.pack("@I",c))
print "Done."

print "Generating unicode_to_lowercase.dat"
with open("unicode_to_lowercase.dat","w") as f:
	for codepoint,cpi in sorted(UnicodeData.data.iteritems()):
		if cpi.lowercase_folding:
			f.write(struct.pack("@I",codepoint))
			f.write(struct.pack("@I",len(cpi.lowercase_folding)))
			for c in cpi.lowercase_folding:
				f.write(struct.pack("@I",c))
print "Done."


#Decomposition
#This is used for "stripping accents" in Wiktionary. that's probably not even a good idea
#few codepoints (<6000) have canonical decomposition, so we use a sparse map here.
print "Generating unicode_canonical_decomposition.dat"
with open("unicode_canonical_decomposition.dat","w") as f:
	for codepoint,cpi in UnicodeData.data.iteritems():
		if cpi.decomposition and cpi.decomposition_type==None:
			f.write(struct.pack("@I",codepoint))
			f.write(struct.pack("@I",len(cpi.decomposition)))
			for decomposition_codepoint in cpi.decomposition:
				f.write(struct.pack("@I",decomposition_codepoint))
print "Done"


#findx-specific decomposition
#This is used for decomposing codepoints, then examining which combining marks should be removed, and then composing again.
#This includes the canonical and compatible decompositions, except:
#  - compatibility-decompositions to a single codepoint, eg. microsign μ (U+00B5) ->  u (U+03BC),
#  - any decomposition that doesn't result in at least one combining mark/diacritic
#  - isn't alphabetic
#The other marks categories:
#  - "Mc" are spacing marks and mostly used for vowel signs.
#  - "Me" enclosing marks cyrillic hundred/thousand/million/... modifiers to letters apparently used in church slavonic, or they are enclosing circle/diamond/square/...
def any_combining_marks(decomposition):
	for codepoint in decomposition:
		if codepoint in UnicodeData.data and UnicodeData.data[codepoint].general_category=="Mn":
			return True #nonspacing mark
		if codepoint in UnicodeData.data:
			if UnicodeData.data[codepoint].decomposition:
				#multi-layer decomposition. Guess that it does have one or more combining marks.
				#this case only appears for 4 codepoints in unicode v10.0 data (Ǆ/ǅ/ǆ/ﬅ)
				return True
	return False

print "Generating unicode_combining_mark_decomposition.dat"
with open("unicode_combining_mark_decomposition.dat","w") as f:
	for codepoint,cpi in UnicodeData.data.iteritems():
		if "Alphabetic" in cpi.derived_core_props and cpi.decomposition and len(cpi.decomposition)>1:
			generate_decomposition = False
			if cpi.decomposition_type==None: #canonical
				generate_decomposition = True
			elif cpi.decomposition_type=="compat":
				if any_combining_marks(cpi.decomposition):
					generate_decomposition = True
			if generate_decomposition:
				if is_interesting(codepoint): print "U+%04X: '%s' : decompose"%(codepoint,unichr(codepoint))
				f.write(struct.pack("@I",codepoint))
				f.write(struct.pack("@I",len(cpi.decomposition)))
				for decomposition_codepoint in cpi.decomposition:
					f.write(struct.pack("@I",decomposition_codepoint))
			else:
				if is_interesting(codepoint): print "U+%04X: '%s' : no decomposition"%(codepoint,unichr(codepoint))
		else:
			if is_interesting(codepoint): print "U+%04X: '%s' : no decomposition entry (non-alfa/no-comp/comp-len<2"%(codepoint,unichr(codepoint))
print "Done"
