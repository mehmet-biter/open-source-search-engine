#ifndef RESULTOVERRIDE_H
#define RESULTOVERRIDE_H

#include <string>

class Url;

class ResultOverride {
public:
	ResultOverride(const std::string &title, const std::string &summary);

	std::string getTitle(const Url &url) const;
	std::string getSummary(const Url &url) const;

private:
	std::string m_title;
	std::string m_summary;

	size_t m_titleDomainPos;
	bool m_needsTitleDomainReplacement;
	size_t m_titleHostPos;
	bool m_needsTitleHostReplacement;

	size_t m_summaryDomainPos;
	bool m_needsSummaryDomainReplacement;

	size_t m_summaryHostPos;
	bool m_needsSummaryHostReplacement;
};

#endif //RESULTOVERRIDE_H
