#include "tokenizer.h"

void xml_tokenizer_phase_2(const Xml * /*xml*/, lang_t lang, const char *country_code, TokenizerResult *tr) {
	plain_tokenizer_phase_2(lang,country_code,tr);
	//TODO: look for <super> with single-digit content and make superscript codepoints and join with sorrounding word
	//TODO: look for <sub> with single-digit content and make subscript codepoints and join with sorrounding word
}

