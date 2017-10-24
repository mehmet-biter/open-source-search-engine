#include "ResultOverride.h"
#include "Url.h"

ResultOverride::ResultOverride(const std::string &title, const std::string &summary)
	: m_title(title)
	, m_summary(summary)
	, m_titleDomainPos(m_title.find("{DOMAIN}"))
	, m_needsTitleDomainReplacement(m_titleDomainPos != std::string::npos)
	, m_titleHostPos(m_title.find("{HOST}"))
	, m_needsTitleHostReplacement(m_titleHostPos != std::string::npos)
	, m_summaryDomainPos(m_summary.find("{DOMAIN}"))
	, m_needsSummaryDomainReplacement(m_summaryDomainPos != std::string::npos)
	, m_summaryHostPos(m_summary.find("{HOST}"))
	, m_needsSummaryHostReplacement(m_summaryHostPos != std::string::npos) {
}


std::string ResultOverride::getTitle(const Url &url) const {
	std::string title(m_title);

	if (m_needsTitleDomainReplacement) {
		title.replace(m_titleDomainPos, 8, url.getDomain(), url.getDomainLen());
	}

	if (m_needsTitleHostReplacement) {
		title.replace(m_titleHostPos, 6, url.getHost(), url.getHostLen());
	}

	return title;
}

std::string ResultOverride::getSummary(const Url &url) const {
	std::string summary(m_summary);

	if (m_needsSummaryDomainReplacement) {
		summary.replace(m_summaryDomainPos, 8, url.getDomain(), url.getDomainLen());
	}

	if (m_needsSummaryHostReplacement) {
		summary.replace(m_summaryHostPos, 6, url.getHost(), url.getHostLen());
	}

	return summary;
}
