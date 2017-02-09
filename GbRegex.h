#ifndef GB_GBREGEX_H
#define GB_GBREGEX_H

#include <pcre.h>
#include <string>

class GbRegex {
public:
	GbRegex(const char *pattern, int options = 0, int study_options = 0);
	GbRegex(const GbRegex &rhs);

	~GbRegex();

	bool match(const char *subject, int options = 0) const;
	bool hasError() const;
private:
	std::string m_pattern;

	pcre *m_pcre;
	pcre_extra *m_extra;

	int m_compile_options;
	const char *m_compile_error;
	int m_compile_error_offset;

	int m_study_options;
	const char *m_study_error;
};


#endif //GB_GBREGEX_H
