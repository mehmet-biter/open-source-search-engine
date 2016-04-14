#ifndef GB_URLPARSER_H
#define GB_URLPARSER_H

#include "UrlComponent.h"
#include <vector>
#include <map>

class UrlParser {
public:
	UrlParser( const char *url, size_t urlLen );

	// path
	bool removePath( const UrlComponent::Matcher &keyMatch, const UrlComponent::Validator &validator );

	// query
	std::vector<UrlComponent*> matchQuery( const UrlComponent::Matcher &keyMatch );

	
	bool removeQuery( const std::vector<UrlComponent*> &urlComponents );
	bool removeQuery( const UrlComponent::Matcher &keyMatch );

	bool removeQuery( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator );
	bool removeQuery( const UrlComponent::Matcher &keyMatch, const UrlComponent::Validator &validator );

	const char* unparse();

private:
	void parse();

	const char *m_url;
	size_t m_urlLen;

	const char *m_scheme;
	size_t m_schemeLen;

	const char *m_domain; // including port (for now)
	size_t m_domainLen;

	std::vector<UrlComponent> m_paths;
	char m_pathEndChar;

	std::vector<UrlComponent> m_queries;
	std::map<std::string, size_t> m_queriesMap;

	std::string m_urlParsed;
};

#endif // GB_URLPARSER_H
