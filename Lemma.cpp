#include "Lemma.h"

sto::Lexicon lemma_lexicon;

bool load_lemma_lexicon() {
	return lemma_lexicon.load("lexicon_da.sto");
}

