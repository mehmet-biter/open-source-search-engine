#ifndef GB_URLPARSER_H
#define GB_URLPARSER_H

#include "UrlComponent.h"
#include <vector>
#include <map>

class UrlParser {
public:
	UrlParser( const char *url, size_t urlLen );

	void deleteComponent( UrlComponent *urlComponent );

	// path
	std::vector<std::pair<UrlComponent*, UrlComponent*> > matchPath( const UrlComponent::Matcher &keyMatch );

	bool removePath( const std::vector<std::pair<UrlComponent*, UrlComponent*> > &urlComponents, const UrlComponent::Validator &validator );
	bool removePath( const UrlComponent::Matcher &keyMatch, const UrlComponent::Validator &validator );

	// path param
	std::vector<UrlComponent*> matchPathParam( const UrlComponent::Matcher &keyMatch );

	bool removePathParam( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator );
	bool removePathParam( const UrlComponent::Matcher &matcher, const UrlComponent::Validator &validator );

	// query
	size_t getQueryParamCount() {
		return m_queries.size();
	}

	std::vector<UrlComponent*> matchQueryParam( const UrlComponent::Matcher &keyMatch );

	bool removeQueryParam( const char *param );
	bool removeQueryParam( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator );
	bool removeQueryParam( const UrlComponent::Matcher &keyMatch, const UrlComponent::Validator &validator );

	const char* unparse();

private:
	void parse();

	bool removeComponent( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator );

	const char *m_url;
	size_t m_urlLen;

	const char *m_scheme;
	size_t m_schemeLen;

	const char *m_hostName; // including port (for now)
	size_t m_hostNameLen;

	std::vector<UrlComponent> m_paths;
	char m_pathEndChar;
	size_t m_pathsDeleteCount;

	std::vector<UrlComponent> m_queries;
	std::map<std::string, size_t> m_queriesMap;
	size_t m_queriesDeleteCount;

	std::string m_urlParsed;
};

#endif // GB_URLPARSER_H
