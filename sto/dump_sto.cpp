#include "sto.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>


static const char *part_of_speech_str(sto::part_of_speech_t pos) {
	switch(pos) {
		case sto::part_of_speech_t::adjective:	return "adjective";
		case sto::part_of_speech_t::commonNoun:	return "commonNoun";
		case sto::part_of_speech_t::conjunction:	return "conjunction";
		case sto::part_of_speech_t::demonstrativePronoun:	return "demonstrativePronoun";
		case sto::part_of_speech_t::deponentVerb:	return "deponentVerb";
		case sto::part_of_speech_t::existentialPronoun:	return "existentialPronoun";
		case sto::part_of_speech_t::generalAdverb:	return "generalAdverb";
		case sto::part_of_speech_t::indefinitePronoun:	return "indefinitePronoun";
		case sto::part_of_speech_t::infinitiveParticle:	return "infinitiveParticle";
		case sto::part_of_speech_t::interjection:	return "interjection";
		case sto::part_of_speech_t::interrogativeRelativePronoun:	return "interrogativeRelativePronoun";
		case sto::part_of_speech_t::mainVerb:	return "mainVerb";
		case sto::part_of_speech_t::numeral:	return "numeral";
		case sto::part_of_speech_t::ordinalAdjective:	return "ordinalAdjective";
		case sto::part_of_speech_t::personalPronoun:	return "personalPronoun";
		case sto::part_of_speech_t::possessivePronoun:	return "possessivePronoun";
		case sto::part_of_speech_t::preposition:	return "preposition";
		case sto::part_of_speech_t::properNoun:	return "properNoun";
		case sto::part_of_speech_t::reciprocalPronoun:	return "reciprocalPronoun";
		case sto::part_of_speech_t::unclassifiedParticle:	return "unclassifiedParticle";
		case sto::part_of_speech_t::unspecified:	return "unspecified";
		case sto::part_of_speech_t::coordinatingConjunction:	return "coordinatingConjunction";
		case sto::part_of_speech_t::subordinatingConjunction:	return "subordinatingConjunction";
		default: return "?";
	}
}


static const char *word_form_attribute(sto::word_form_attribute_t a) {
	switch(a) {
		case sto::word_form_attribute_t::none:	return "none";
		case sto::word_form_attribute_t::adjectivalFunction_attributiveFunction:	return "adjectivalFunction_attributiveFunction";
		case sto::word_form_attribute_t::adjectivalFunction_predicativeFunction:	return "adjectivalFunction_predicativeFunction";
		case sto::word_form_attribute_t::adjectivalFunction_unspecified:	return "adjectivalFunction_unspecified";
		case sto::word_form_attribute_t::case_genitiveCase:	return "case_genitiveCase";
		case sto::word_form_attribute_t::case_nominativeCase:	return "case_nominativeCase";
		case sto::word_form_attribute_t::case_unspecified:	return "case_unspecified";
		case sto::word_form_attribute_t::definiteness_definite:	return "definiteness_definite";
		case sto::word_form_attribute_t::definiteness_indefinite:	return "definiteness_indefinite";
		case sto::word_form_attribute_t::definiteness_unspecified:	return "definiteness_unspecified";
		case sto::word_form_attribute_t::degree_comparative:	return "degree_comparative";
		case sto::word_form_attribute_t::degree_positive:	return "degree_positive";
		case sto::word_form_attribute_t::degree_superlative:	return "degree_superlative";
		case sto::word_form_attribute_t::grammaticalGender_commonGender:	return "grammaticalGender_commonGender";
		case sto::word_form_attribute_t::grammaticalGender_neuter:	return "grammaticalGender_neuter";
		case sto::word_form_attribute_t::grammaticalGender_unspecified:	return "grammaticalGender_unspecified";
		case sto::word_form_attribute_t::grammaticalNumber_plural:	return "grammaticalNumber_plural";
		case sto::word_form_attribute_t::grammaticalNumber_singular:	return "grammaticalNumber_singular";
		case sto::word_form_attribute_t::grammaticalNumber_unspecified:	return "grammaticalNumber_unspecified";
		case sto::word_form_attribute_t::independentWord_no:	return "independentWord_no";
		case sto::word_form_attribute_t::independentWord_yes:	return "independentWord_yes";
		case sto::word_form_attribute_t::officiallyApproved_no:	return "officiallyApproved_no";
		case sto::word_form_attribute_t::officiallyApproved_yes:	return "officiallyApproved_yes";
		case sto::word_form_attribute_t::ownerNumber_plural:	return "ownerNumber_plural";
		case sto::word_form_attribute_t::ownerNumber_singular:	return "ownerNumber_singular";
		case sto::word_form_attribute_t::ownerNumber_unspecified:	return "ownerNumber_unspecified";
		case sto::word_form_attribute_t::person_firstPerson:	return "person_firstPerson";
		case sto::word_form_attribute_t::person_secondPerson:	return "person_secondPerson";
		case sto::word_form_attribute_t::person_thirdPerson:	return "person_thirdPerson";
		case sto::word_form_attribute_t::reflexivity_no:	return "reflexivity_no";
		case sto::word_form_attribute_t::reflexivity_yes:	return "reflexivity_yes";
		case sto::word_form_attribute_t::reflexivity_unspecified:	return "reflexivity_unspecified";
		case sto::word_form_attribute_t::register_formalRegister:	return "register_formalRegister";
		case sto::word_form_attribute_t::register_OBSOLETE:	return "register_OBSOLETE";
		case sto::word_form_attribute_t::tense_past:	return "tense_past";
		case sto::word_form_attribute_t::tense_present:	return "tense_present";
		case sto::word_form_attribute_t::transcategorization_transadjectival:	return "transcategorization_transadjectival";
		case sto::word_form_attribute_t::transcategorization_transadverbial:	return "transcategorization_transadverbial";
		case sto::word_form_attribute_t::transcategorization_transnominal:	return "transcategorization_transnominal";
		case sto::word_form_attribute_t::verbFormMood_gerundive:	return "verbFormMood_gerundive";
		case sto::word_form_attribute_t::verbFormMood_imperative:	return "verbFormMood_imperative";
		case sto::word_form_attribute_t::verbFormMood_indicative:	return "verbFormMood_indicative";
		case sto::word_form_attribute_t::verbFormMood_infinitive:	return "verbFormMood_infinitive";
		case sto::word_form_attribute_t::verbFormMood_participle:	return "verbFormMood_participle";
		case sto::word_form_attribute_t::voice_activeVoice:	return "voice_activeVoice";
		case sto::word_form_attribute_t::voice_passiveVoice:	return "voice_passiveVoice";
		default: return "?";
	}
}


int main(int argc, char **argv) {
	if(argc!=2 || strcmp(argv[1],"-?")==0 || strcmp(argv[1],"--help")==0) {
		fprintf(stderr,"usage: %s <sto-file>\n", argv[0]);
		return 1;
	}
	
	sto::Lexicon l;
	if(!l.load(argv[1])) {
		fprintf(stderr,"Could not open %s. Possible error: %s\n",argv[1],strerror(errno));
		return 2;
	}
	
	unsigned lexical_entry_count = 0;
	unsigned word_form_count = 0;
	for(auto le = l.first_entry(); le; le=l.next_entry(le)) {
		printf("===== %s\n", part_of_speech_str(le->part_of_speech));
		auto v(le->query_all_explicit_ford_forms());
		for(auto wf : v) {
			printf("%.*s",(int)wf->written_form_length, wf->written_form);
			for(unsigned j=0; j<sto::WordForm::max_attributes; j++)
				if(wf->attribute[j]!=sto::word_form_attribute_t::none)
					printf(" %s",word_form_attribute(wf->attribute[j]));
			printf("\n");
			word_form_count++;
		}
		lexical_entry_count++;
	}
	printf("lexical_entry_count: %u\n", lexical_entry_count);
	printf("word_form_count: %u\n", word_form_count);
	
	return 0;
}
