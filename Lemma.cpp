#include "Lemma.h"
#include "Lexicons.h"


sto::Lexicon *lemma_lexicon = nullptr;

bool load_lemma_lexicon() {
	lemma_lexicon = getLexicon("lexicon_da.sto");
	return lemma_lexicon!=nullptr;
}
