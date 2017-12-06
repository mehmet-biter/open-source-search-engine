#include "sto.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>


//static const char version_1_signature[80] = "parsed-sto-v2\n";
static const char version_2_signature[80] = "parsed-sto-v2\n";

std::vector<const sto::WordForm *> sto::LexicalEntry::query_all_explicit_word_forms() const {
	std::vector<const WordForm*> entries;
	const char *p = reinterpret_cast<const char*>(query_first_explicit_word_form());
	for(unsigned i=0; i<explicit_word_form_count; i++) {
		const WordForm *e = reinterpret_cast<const WordForm*>(p);
		entries.push_back(e);
		p += e->size();
	}
	return entries;
}


const sto::WordForm *sto::LexicalEntry::find_first_wordform(const std::string &word) const {
	const char *p = reinterpret_cast<const char*>(query_first_explicit_word_form());
	for(unsigned i=0; i<explicit_word_form_count; i++) {
		const WordForm *e = reinterpret_cast<const WordForm*>(p);
		if(e->written_form_length==word.length() &&
		   memcmp(e->written_form,word.data(),e->written_form_length)==0)
			return e;
		p += e->size();
	}
	return NULL;
}


bool sto::Lexicon::load(const std::string &filename) {
	unload();
	
	int fd = open(filename.c_str(), O_RDONLY);
	if(fd<0)
		return false;
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		::close(fd);
		return false;
	}
	if((size_t)st.st_size<sizeof(version_2_signature)) {
		::close(fd);
		return false;
	}
	
	mapped_memory_start = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if(mapped_memory_start==MAP_FAILED) {
		::close(fd);
		return false;
	}
	::close(fd);
	
	mapped_memory_size = st.st_size;
	
	if(memcmp(mapped_memory_start,version_2_signature,sizeof(version_2_signature))!=0) {
		unload();
		return false;
	}
	
	//parse and index the entries
	//see sto_structure.txt for details
	const char *start = reinterpret_cast<const char*>(mapped_memory_start);
	const char *end = start + mapped_memory_size;
	const char *p = start + sizeof(version_2_signature);
	while(p<end) {
		const LexicalEntry *le = reinterpret_cast<const LexicalEntry*>(p);
		p = reinterpret_cast<const char*>(le->query_first_explicit_word_form());
		for(unsigned i=0; i<le->explicit_word_form_count; i++) {
			const WordForm *wf = reinterpret_cast<const WordForm*>(p);
			const char *p2 = p+wf->size();
			if(p2>end)
				return false;
			
			entries.emplace(std::string(wf->written_form,wf->written_form_length),le);
			p = p2;
		}
		morphological_unit_id_entries.emplace(std::string(le->query_morphological_unit_id(), le->morphological_unit_id_len),le);
	}
	
	//todo:removed non-duplicates from query_morphological_unit_id
//	for(std::multimap<std::string,const LexicalEntry*>::iterator iter = morphological_unit_id_entries.begin(); iter!=morphological_unit_id_entries.end()) {
//		std::multimap<std::string,const LexicalEntry*>::iterator next(iter);
//		++next;
//		if(next!=morphological_unit_id_entries.end()
//	}
	
	return true;
}


void sto::Lexicon::unload() {
	if(mapped_memory_size!=0) {
		(void)munmap(mapped_memory_start,mapped_memory_size);
		mapped_memory_start = NULL;
		mapped_memory_size = 0;
	}
	entries.clear();
	morphological_unit_id_entries.clear();
}



const sto::LexicalEntry *sto::Lexicon::lookup(const std::string &word) const {
	auto iter = entries.find(word);
	if(iter!=entries.end())
		return iter->second;
	else
		return 0;
}


std::vector<const sto::LexicalEntry *> sto::Lexicon::query_matches(const std::string &word) const {
	auto lower = entries.lower_bound(word);
	auto upper = entries.upper_bound(word);
	std::vector<const LexicalEntry *> entries;
	for(auto iter=lower; iter!=upper; ++iter)
		entries.push_back(iter->second);
	return entries;
}



const sto::LexicalEntry *sto::Lexicon::first_entry() const {
	const char *start = reinterpret_cast<const char*>(mapped_memory_start);
	const char *p = start + sizeof(version_2_signature);
	return reinterpret_cast<const LexicalEntry*>(p);
}


const sto::LexicalEntry *sto::Lexicon::next_entry(const LexicalEntry *le) const {
	const char *p = reinterpret_cast<const char*>(le);
	const char *start = reinterpret_cast<const char*>(mapped_memory_start);
	const char *end = start + mapped_memory_size;
	if(p<start || p>=end)
		return NULL;
	p = reinterpret_cast<const char*>(le->query_first_explicit_word_form());
	for(unsigned i=0; i<le->explicit_word_form_count; i++) {
		const WordForm *wf = reinterpret_cast<const WordForm*>(p);
		const char *p2 = p+wf->size();
		if(p2>end)
			return NULL;
		p = p2;
	}
	if(p<end)
		return reinterpret_cast<const LexicalEntry*>(p);
	else
		return NULL;
}


std::vector<const sto::LexicalEntry *> sto::Lexicon::query_lexical_entries_with_same_morphological_unit_id(const sto::LexicalEntry *le) const {
	std::vector<const sto::LexicalEntry *> v;
	auto range = morphological_unit_id_entries.equal_range(std::string(le->query_morphological_unit_id(),le->morphological_unit_id_len));
	for(auto iter=range.first; iter!=range.second; ++iter) {
		v.push_back(iter->second);
	}
	return v;
}


#ifdef UNITTEST
#include <assert.h>
#include <stdio.h>

using namespace sto;

int main(void) {
	//plain ctor
	{
		Lexicon l;
		assert(l.lookup("foo")==NULL);
		auto v(l.query_matches("foo"));
		assert(v.empty());
	}
	
	//nonexisting file
	{
		::unlink("sto.unittest");
		Lexicon l;
		assert(!l.load("sto.unittest"));
	}
	
	//empty file
	{
		int fd = open("sto.unittest",O_WRONLY|O_CREAT|O_TRUNC,0666);
		close(fd);
		Lexicon l;
		assert(!l.load("sto.unittest"));
	}
	
	//file with wrong signature
	{
		int fd = open("sto.unittest",O_WRONLY|O_CREAT|O_TRUNC,0666);
		write(fd,"hello world",11);
		for(int i=0; i<10; i++)
			write(fd,"0123456789abcdef",16);
		close(fd);
		Lexicon l;
		assert(!l.load("sto.unittest"));
	}
	
	//file with just the signature
	{
		int fd = open("sto.unittest",O_WRONLY|O_CREAT|O_TRUNC,0666);
		write(fd,version_2_signature,sizeof(version_2_signature));
		close(fd);
		Lexicon l;
		assert(l.load("sto.unittest"));
		assert(l.lookup("foo")==NULL);
	}
	
	//file with one lexical entry
	//0: foo foos
	{
		int fd = open("sto.unittest",O_WRONLY|O_CREAT|O_TRUNC,0666);
		char tmp[16];
		write(fd,version_2_signature,sizeof(version_2_signature));
		//le#0
		tmp[0] = (char)part_of_speech_t::commonNoun;
		write(fd, tmp, 1);
		tmp[0] = (char)word_form_type_t::wordFormsExplicit;
		write(fd, tmp, 1);
		write(fd, "\006",1); //morph-unit-id len
		write(fd,"\002",1); //wordforms
		write(fd, "morph1",6); //morph-unit-id
		//le#0:wf#0
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::degree_positive;
		write(fd,tmp,6);
		write(fd,"\003foo",4);
		//le#0:wf#1
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::case_nominativeCase;
		write(fd,tmp,6);
		write(fd,"\004foos",5);
		close(fd);
		Lexicon l;
		assert(l.load("sto.unittest"));
		assert(l.lookup("foo")!=NULL);
		assert(l.lookup("foos")!=NULL);
		assert(l.lookup("fooz")==NULL);
		auto e0(l.lookup("foo"));
		auto e1(l.lookup("foos"));
		assert(e0==e1);
		assert(e0->part_of_speech==part_of_speech_t::commonNoun);
		auto wf0(e0->find_first_wordform("foo"));
		assert(wf0);
		assert(wf0->has_attribute(word_form_attribute_t::none));
		assert(wf0->has_attribute(word_form_attribute_t::degree_positive));
		assert(!wf0->has_attribute(word_form_attribute_t::person_thirdPerson));
		auto wf1(e1->find_first_wordform("foos"));
		assert(wf1);
		assert(wf1->has_attribute(word_form_attribute_t::none));
		assert(wf1->has_attribute(word_form_attribute_t::case_nominativeCase));
		assert(!wf1->has_attribute(word_form_attribute_t::person_thirdPerson));
		auto wf2(e0->find_first_wordform("xxxx"));
		assert(!wf2);
	}
	
	
	//file with three lexical entries
	//0: foo foos
	//1: boo boos
	//2: goo foo boo
	{
		int fd = open("sto.unittest",O_WRONLY|O_CREAT|O_TRUNC,0666);
		char tmp[16];
		write(fd,version_2_signature,sizeof(version_2_signature));
		
		//le#0
		tmp[0] = (char)part_of_speech_t::commonNoun;
		write(fd, tmp, 1);
		tmp[0] = (char)word_form_type_t::wordFormsExplicit;
		write(fd, tmp, 1);
		write(fd,"\006",1); //morph-unit-id len
		write(fd,"\002",1); //#wordforms
		write(fd, "morph1",6); //morph-unit-id
		//le#0:wf#0
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		write(fd,tmp,6);
		write(fd,"\003foo",4);
		//le#0:wf#1
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::case_nominativeCase;
		write(fd,tmp,6);
		write(fd,"\004foos",5);
		
		//le#1
		tmp[0] = (char)part_of_speech_t::commonNoun;
		write(fd, tmp, 1);
		tmp[0] = (char)word_form_type_t::wordFormsExplicit;
		write(fd, tmp, 1);
		write(fd,"\006",1); //morph-unit-id len
		write(fd,"\002",1); //#wordforms
		write(fd, "morph2",6); //morph-unit-id
		//le#1:wf#0
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		write(fd,tmp,6);
		write(fd,"\003boo",4);
		//le#1:wf#1
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::case_nominativeCase;
		write(fd,tmp,6);
		write(fd,"\004boos",5);
		
		//le#2
		tmp[0] = (char)part_of_speech_t::commonNoun;
		write(fd, tmp, 1);
		tmp[0] = (char)word_form_type_t::wordFormsExplicit;
		write(fd, tmp, 1);
		write(fd,"\006",1); //morph-unit-id len
		write(fd,"\003",1); //#wordforms
		write(fd, "morph1",6); //morph-unit-id
		//le#2:wf#0
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		write(fd,tmp,6);
		write(fd,"\003goo",4);
		//le#2:wf#1
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::case_nominativeCase;
		write(fd,tmp,6);
		write(fd,"\003foo",4);
		//le#2:wf#2
		tmp[0]=tmp[1]=tmp[2]=tmp[3]=tmp[4]=tmp[5] = (char)word_form_attribute_t::none;
		tmp[0]=(char)word_form_attribute_t::case_nominativeCase;
		write(fd,tmp,6);
		write(fd,"\003boo",4);
		
		close(fd);
		
		Lexicon l;
		assert(l.load("sto.unittest"));
		assert(l.lookup("foo")!=NULL);
		assert(l.lookup("foos")!=NULL);
		assert(l.lookup("boo")!=NULL);
		assert(l.lookup("foos")!=NULL);
		assert(l.lookup("goo")!=NULL);
		
		auto v0(l.query_matches("foo"));
		assert(v0.size()==2);
		auto v1(l.query_matches("foos"));
		assert(v1.size()==1);
		auto v2(l.query_matches("boo"));
		assert(v2.size()==2);
		auto v3(l.query_matches("boos"));
		assert(v3.size()==1);
		auto v4(l.query_matches("goo"));
		assert(v4.size()==1);
		
		assert(v0[0]==v1[0] || v0[1]==v1[0]);
		
		auto m0 = l.query_lexical_entries_with_same_morphological_unit_id(l.lookup("foos"));
		assert(m0.size()==2);
		assert(m0[0]!=m0[1]);
		
		auto m1 = l.query_lexical_entries_with_same_morphological_unit_id(l.lookup("boos"));
		assert(m1.size()==1);
		assert(m1[0]==l.lookup("boos"));
	}
	
}
#endif
