#include "GbRegex.h"
#include "Log.h"
#include <string.h>

GbRegex::GbRegex(const char *pattern, int options, int study_options)
	: m_pattern(pattern)
	, m_pcre(NULL)
	, m_extra(NULL)
	, m_compile_options(options)
	, m_compile_error(NULL)
	, m_compile_error_offset(0)
	, m_study_options(study_options)
	, m_study_error(NULL) {
	m_pcre = pcre_compile(m_pattern.c_str(), m_compile_options, &m_compile_error, &m_compile_error_offset, NULL);
	if (m_pcre) {
		m_extra = pcre_study(m_pcre, m_study_options, &m_study_error);

		if (m_study_error) {
			log(LOG_INFO, "GbRegex: pcre_study error='%s'", m_study_error);
		}
	} else {
		if (m_compile_error) {
			log(LOG_INFO, "GbRegex: pcre_compile error='%s'", m_compile_error);
		}
	}
}

GbRegex::GbRegex(const GbRegex& rhs)
	: m_pattern(rhs.m_pattern)
	, m_pcre(NULL)
	, m_extra(NULL)
	, m_compile_options(rhs.m_compile_options)
	, m_compile_error(NULL)
	, m_compile_error_offset(0)
	, m_study_options(rhs.m_study_options)
	, m_study_error(NULL) {
	m_pcre = pcre_compile(m_pattern.c_str(), m_compile_options, &m_compile_error, &m_compile_error_offset, NULL);
	if (m_pcre) {
		m_extra = pcre_study(m_pcre, m_study_options, &m_study_error);

		if (m_study_error) {
			log(LOG_INFO, "GbRegex: pcre_study error='%s'", m_study_error);
		}
	} else {
		if (m_compile_error) {
			log(LOG_INFO, "GbRegex: pcre_compile error='%s'", m_compile_error);
		}
	}
}

GbRegex::~GbRegex() {
	if (m_extra) {
		pcre_free_study(m_extra);
	}

	if (m_pcre) {
		pcre_free(m_pcre);
	}
}

bool GbRegex::match(const char *subject, int options) const {
	if (hasError()) {
		log(LOG_WARN, "GbRegex: has error during compilation(%s) or study(%s)", m_compile_error, m_study_error);
		return false;
	}

	return (pcre_exec(m_pcre, m_extra, subject, strlen(subject), 0, options, NULL, 0) >= 0);
}

bool GbRegex::hasError() const {
	return (m_compile_error != NULL || m_study_error != NULL);
}