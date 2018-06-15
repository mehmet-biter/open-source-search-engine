#!/usr/bin/python3
import xml.etree.ElementTree
import struct
import argparse
import sys
import os


part_of_speech_map={
	"adjective":1,
	"commonNoun":2,
	"conjunction":3,
	"demonstrativePronoun":4,
	"deponentVerb":5,
	"existentialPronoun":6,
	"generalAdverb":7,
	"indefinitePronoun":8,
	"infinitiveParticle":9,
	"interjection":10,
	"interrogativeRelativePronoun":11,
	"mainVerb":12,
	"numeral":13,
	"ordinalAdjective":14,
	"personalPronoun":15,
	"possessivePronoun":16,
	"preposition":17,
	"properNoun":18,
	"reciprocalPronoun":19,
	"unclassifiedParticle":20,
	"unspecified":21,
	"coordinatingConjunction":22,
	"subordinatingConjunction":23
}


word_form_attribute_map={
	"adjectivalFunction_attributiveFunction": 1,
	"adjectivalFunction_predicativeFunction": 2,
	"adjectivalFunction_unspecified": 3,
	"case_genitiveCase": 4,
	"case_nominativeCase": 5,
	"case_unspecified": 6,
	"definiteness_definite": 7,
	"definiteness_indefinite": 8,
	"definiteness_unspecified": 9,
	"degree_comparative": 10,
	"degree_positive": 11,
	"degree_superlative": 12,
	"grammaticalGender_commonGender": 13,
	"grammaticalGender_neuter": 14,
	"grammaticalGender_unspecified": 15,
	"grammaticalNumber_plural": 16,
	"grammaticalNumber_singular": 17,
	"grammaticalNumber_unspecified": 18,
	"independentWord_no": 19,
	"independentWord_yes": 20,
	"officiallyApproved_no": 21,
	"officiallyApproved_yes": 22,
	"ownerNumber_plural": 23,
	"ownerNumber_singular": 24,
	"ownerNumber_unspecified": 25,
	"person_firstPerson": 26,
	"person_secondPerson": 27,
	"person_thirdPerson": 28,
	"reflexivity_no": 29,
	"reflexivity_yes": 30,
	"reflexivity_unspecified": 31,
	"register_formalRegister": 32,
	"register_OBSOLETE": 33,
	"tense_past": 34,
	"tense_present": 35,
	"transcategorization_transadjectival": 36,
	"transcategorization_transadverbial": 37,
	"transcategorization_transnominal": 38,
	"verbFormMood_gerundive": 39,
	"verbFormMood_imperative": 40,
	"verbFormMood_indicative": 41,
	"verbFormMood_infinitive": 42,
	"verbFormMood_participle": 43,
	"voice_activeVoice": 44,
	"voice_passiveVoice": 45
}



total_entry_count = None
total_wordform_count = None


def process_lexcial_entry(lexicalentry,output_file):
	global total_entry_count, total_wordform_count
	
	part_of_speech=None
	id=None
	morphological_unit_id=None
	for feat in lexicalentry.findall("feat"):
		att=feat.attrib["att"]
		val=feat.attrib["val"]
		#print("lexicalentry.feat: att=%s val=%s"%(att,val))
		if att=="partOfSpeech":
			if val in part_of_speech_map:
				part_of_speech = part_of_speech_map[val]
			else:
				print("Unknown part_of_speech: ",val, file=sys.stderr)
				sys.exit(2)
		elif att=="id":
			id=val
		elif att=="morphologicalUnitId":
			morphological_unit_id=val
		#todo:decomposition
	if part_of_speech==None:
		print("Entry %s doesn't have partOfSpeech"%id, file=sys.stderr)
	if morphological_unit_id==None:
		print("Entry %s doesn't have morphologicalUnitId"%id, file=sys.stderr)
		sys.exit(2)
	
	raw_wordforms = b""
	wordform_count = 0
	
	for wordform in lexicalentry.findall("WordForm"):
		attributes=[]
		for feat in wordform.findall("feat"):
			att=feat.attrib["att"]
			val=feat.attrib["val"]
			#print("wordform.feat: att=%s val=%s"%(att,val))
			s=att+"_"+val
			if s in word_form_attribute_map:
				attributes.append(word_form_attribute_map[s])
			else:
				print("Entry %s: Unknown wordform feat: %s"%(id,s),file=sys.stderr)
				sys.exit(2)
		if len(attributes)==0:
			print("Entry %s: No feat?"%(id),file=sys.stderr)
			#happens for a few entries such as "Chippendale". We convert it anyway beucase at least we know the part-of-speech
			#sys.exit(2)
		if len(attributes)>6:
			print("Entry %s: Too many feat (%d)"%(id,len(attributes)),file=sys.stderr)
			sys.exit(2)
		while len(attributes)<6:
			attributes.append(0)
		for formrepresentation in wordform.findall("FormRepresentation"):
			writtenform=None
			for feat in formrepresentation.findall("feat"):
				att=feat.attrib["att"]
				val=feat.attrib["val"]
				if att=="writtenForm":
					writtenform=val
			
			raw_writtenform = writtenform.encode()
			raw_wordform = struct.pack(">BBBBBB",attributes[0],attributes[1],attributes[2],attributes[3],attributes[4],attributes[5]) \
					+ struct.pack(">B",len(raw_writtenform)) \
					+ raw_writtenform
			wordform_count += 1
			raw_wordforms += raw_wordform
	
	raw_morphological_unit_id = morphological_unit_id.encode()
	raw_entry = struct.pack(">BBBB",part_of_speech,1,len(raw_morphological_unit_id),wordform_count) + raw_morphological_unit_id + raw_wordforms
	output_file.write(raw_entry)
	
	total_entry_count += 1
	total_wordform_count += wordform_count


def do_convert_lexicon_file(input_file_name, output_file):
	print("Opening and parsing %s"%(input_file_name))
	tree = xml.etree.ElementTree.parse(input_file_name)
	root = tree.getroot()
	lexicon=root.find("Lexicon")
	global total_entry_count, total_wordform_count
	total_entry_count=0
	total_wordform_count=0
	for lexicalentry in lexicon.findall("LexicalEntry"):
		process_lexcial_entry(lexicalentry,output_file)
	
	print("Done")
	print("\tlexical entries: %d"%total_entry_count)
	print("\twordforms: %d"%total_wordform_count)


def do_convert_lexcialentry_file(input_file_name,output_file):
	print("%s:"%input_file_name);
	tree = xml.etree.ElementTree.parse(input_file_name)
	root = tree.getroot()
	process_lexcial_entry(root,output_file)
	
def do_convert_tree(input_tree_name, output_file):
	global total_entry_count, total_wordform_count
	total_entry_count=0
	total_wordform_count=0
	for (dirpath,dirnames,filenames) in os.walk(input_tree_name):
		for filename in filenames:
			if filename[-4:]==".xml":
				full_file_name = dirpath+"/"+filename
				do_convert_lexcialentry_file(full_file_name,output_file)
	print("Done")
	print("\tlexical entries: %d"%total_entry_count)
	print("\twordforms: %d"%total_wordform_count)


parser = argparse.ArgumentParser(description="STO converter")
parser.add_argument("-i","--input_file",type=str,default=None)
parser.add_argument("-I","--input_tree",type=str,default=None)
parser.add_argument("-o","--output_file",type=str,required=True)
parser.add_argument("command",type=str,default="convert",nargs='?',choices=["convert","signature"])

args=parser.parse_args()

if args.command=="signature" and (args.input_file!=None or args.input_tree!=None):
	print("input_file/input_tree cannot be specified when generating signature", file=sys.stderr)
	sys.exit(1)
if args.command=="convert" and args.input_file==None and args.input_tree==None:
	print("input_file/input_tree and output_file must be specified when generating converting", file=sys.stderr)
	sys.exit(1)


output_file = open(args.output_file,"ab")
if args.command=="signature":
	#simple
	version_1_signature = ("parsed-sto-v2\n"+'\0'*80)[0:80]
	output_file.write(version_1_signature.encode())
elif args.command=="convert":
	if args.input_file:
		do_convert_lexicon_file(args.input_file,output_file)
	else:
		do_convert_tree(args.input_tree,output_file)
else:
	print("argh...", file=sys.stderr)
	sys.exit(99)

output_file.close()
sys.exit(0)
