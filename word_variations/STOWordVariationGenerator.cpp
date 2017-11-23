#include "STOWordVariationGenerator.h"

bool STOWordVariationGenerator::load_lexicon(const char *filename) {
	return lexicon.load(filename);
}
