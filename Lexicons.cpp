#include "Lexicons.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include <map>
#include <memory>

//Yes, I do know that with Greek morphology the plural of lexicon is lexica. But this isn't Greek


static std::map<std::string,std::unique_ptr<sto::Lexicon>> map;
static GbMutex mtx_map;

sto::Lexicon *getLexicon(const std::string &filename) {
	ScopedLock sl(mtx_map);
	auto iter = map.find(filename);
	if(iter!=map.end())
		return iter->second.get();
	sto::Lexicon *l = new sto::Lexicon();
	if(!l->load(filename)) {
		delete l;
		return nullptr;
	}
	map.emplace(filename,std::unique_ptr<sto::Lexicon>(l));
	return l;
	
}


void forgetAllLexicons() {
	map.clear();
}
