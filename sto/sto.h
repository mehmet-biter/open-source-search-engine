#ifndef STO_H_
#define STO_H_

#include <inttypes.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <string>


//Interface for reading/using a processed STO file.

namespace sto {

enum class part_of_speech_t : uint8_t {
	//0 is used for detecting corrupted files
	adjective = 1,
	commonNoun = 2,
	conjunction = 3,
	demonstrativePronoun = 4,
	deponentVerb = 5,
	existentialPronoun = 6,
	generalAdverb = 7,
	indefinitePronoun = 8,
	infinitiveParticle = 9,
	interjection = 10,
	interrogativeRelativePronoun = 11,
	mainVerb = 12,
	numeral = 13,
	ordinalAdjective = 14,
	personalPronoun = 15,
	possessivePronoun = 16,
	preposition = 17,
	properNoun = 18,
	reciprocalPronoun = 19,
	unclassifiedParticle = 20,
	unspecified = 21,
	coordinatingConjunction = 22,
	subordinatingConjunction = 23,
};


enum class word_form_type_t : uint8_t {
	wordFormsExplicit = 1,     //entry has all word forms explicitly listed
};


enum class word_form_attribute_t : uint8_t {
	none = 0,
	adjectivalFunction_attributiveFunction = 1,
	adjectivalFunction_predicativeFunction = 2,
	adjectivalFunction_unspecified = 3,
	case_genitiveCase = 4,
	case_nominativeCase = 5,
	case_unspecified = 6,
	definiteness_definite = 7,
	definiteness_indefinite = 8,
	definiteness_unspecified = 9,
	degree_comparative = 10,
	degree_positive = 11,
	degree_superlative = 12,
	grammaticalGender_commonGender = 13,
	grammaticalGender_neuter = 14,
	grammaticalGender_unspecified = 15,
	grammaticalNumber_plural = 16,
	grammaticalNumber_singular = 17,
	grammaticalNumber_unspecified = 18,
	independentWord_no = 19,
	independentWord_yes = 20,
	officiallyApproved_no = 21,
	officiallyApproved_yes = 22,
	ownerNumber_plural = 23,
	ownerNumber_singular = 24,
	ownerNumber_unspecified = 25,
	person_firstPerson = 26,
	person_secondPerson = 27,
	person_thirdPerson = 28,
	reflexivity_no = 29,
	reflexivity_yes = 30,
	reflexivity_unspecified = 31,
	register_formalRegister = 32,
	register_OBSOLETE = 33,
	tense_past = 34,
	tense_present = 35,
	transcategorization_transadjectival = 36,
	transcategorization_transadverbial = 37,
	transcategorization_transnominal = 38,
	verbFormMood_gerundive = 39,
	verbFormMood_imperative = 40,
	verbFormMood_indicative = 41,
	verbFormMood_infinitive = 42,
	verbFormMood_participle = 43,
	voice_activeVoice = 44,
	voice_passiveVoice = 45,
};


struct WordForm {
	static const unsigned max_attributes = 6;
	word_form_attribute_t attribute[max_attributes];
	uint8_t written_form_length;
	char written_form[];
	size_t size() const { return sizeof(attribute)+sizeof(written_form_length)+written_form_length; }
	bool has_attribute(word_form_attribute_t a) const {
		for(auto attr:attribute)
			if(attr==a)
				return true;
		return false;
	}
};

struct LexicalEntry {
	part_of_speech_t part_of_speech;
	word_form_type_t word_form_type;
	uint8_t morphological_unit_id_len;
	uint8_t explicit_word_form_count;
	//char morphological_unit_id[];
	//char explicit_word_forms[];
	const char *query_morphological_unit_id() const { return reinterpret_cast<const char*>(this) + sizeof(*this); }
	const WordForm *query_first_explicit_word_form() const {
		const char *p = reinterpret_cast<const char*>(this) + sizeof(*this);
		p += morphological_unit_id_len;
		return reinterpret_cast<const WordForm*>(p);
	}
	std::vector<const WordForm *> query_all_explicit_word_forms() const;
	const WordForm *find_first_wordform(const std::string &word) const;
};


class Lexicon {
	Lexicon(const Lexicon&) = delete;
	Lexicon& operator=(const Lexicon&) = delete;
	
	void *mapped_memory_start;
	size_t mapped_memory_size;
	std::multimap<std::string,const LexicalEntry*> entries; //wordform -> entry[]
	std::multimap<std::string,const LexicalEntry*> morphological_unit_id_entries; //morphological_unit_id -> entry[]

public:
	Lexicon() : mapped_memory_start(0), mapped_memory_size(0), entries(), morphological_unit_id_entries() {}
	~Lexicon() { unload(); }
	
	bool load(const std::string &filename);
	void unload();
	
	const LexicalEntry *lookup(const std::string &word) const;
	std::vector<const LexicalEntry *> query_matches(const std::string &word) const;
	
	const LexicalEntry *first_entry() const;
	const LexicalEntry *next_entry(const LexicalEntry *le) const;
	std::vector<const LexicalEntry *> query_lexical_entries_with_same_morphological_unit_id(const LexicalEntry *le) const;
};

} //namespace

#endif
