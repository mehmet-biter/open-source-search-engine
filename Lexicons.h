#ifndef LEXICONREF_H_
#define LEXICONREF_H_
#include "sto/sto.h"

sto::Lexicon *getLexicon(const std::string &filename);
void forgetAllLexicons();

#endif
