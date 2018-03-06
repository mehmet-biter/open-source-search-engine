//
// Copyright (C) 2018 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//

#include "QueryLanguage.h"
#include "Conf.h"
#include "GbUtil.h"

// The protocol is very simple.
// The server Receives queries in the form
//	<query-id>:v1|fx_qlang|fx_blang|fx_country|fx_fe_tld|query_string<NL>
//
// The server responses:
//	<query-id>:<lang>=<score>|<lang>=<score><NL>

QueryLanguage g_queryLanguage;

struct QueryLanguageRequest : public FxClientRequest {
	QueryLanguageRequest(void *context, int timeout_ms, query_language_callback_t callback,
	                     const std::string &qlang_hint, const std::string &blang_hint,
	                     const std::string &country_hint, const std::string &fe_tld_hint,
	                     const std::string &query)
		: FxClientRequest(context, timeout_ms)
		, m_callback(callback)
		, m_qlang_hint(qlang_hint)
		, m_blang_hint(blang_hint)
		, m_country_hint(country_hint)
		, m_fe_tld_hint(fe_tld_hint)
		, m_query(query) {
	}

	query_language_callback_t m_callback;

	std::string m_qlang_hint;
	std::string m_blang_hint;
	std::string m_country_hint;
	std::string m_fe_tld_hint;

	std::string m_query;
};

// v1|fx_qlang|fx_blang|fx_country|fx_fe_tld|query_string
void QueryLanguage::convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) {
	std::shared_ptr<QueryLanguageRequest> request = std::dynamic_pointer_cast<QueryLanguageRequest>(base_request);

	out_buffer->reserve_extra(8 + 1 + 3 +
	                          request->m_qlang_hint.size() + 1 +
	                          request->m_blang_hint.size() + 1 +
	                          request->m_country_hint.size() + 1 +
	                          request->m_fe_tld_hint.size() + 1 +
	                          request->m_query.size() + 1);

	sprintf(out_buffer->end(), "%08x", seq);
	out_buffer->push_back(8);
	out_buffer->end()[0] = ':';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), "v1|", 3);
	out_buffer->push_back(3);

	memcpy(out_buffer->end(), request->m_qlang_hint.data(), request->m_qlang_hint.size());
	out_buffer->push_back(request->m_qlang_hint.size());
	out_buffer->end()[0] = '|';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), request->m_blang_hint.data(), request->m_blang_hint.size());
	out_buffer->push_back(request->m_blang_hint.size());
	out_buffer->end()[0] = '|';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), request->m_country_hint.data(), request->m_country_hint.size());
	out_buffer->push_back(request->m_country_hint.size());
	out_buffer->end()[0] = '|';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), request->m_fe_tld_hint.data(), request->m_fe_tld_hint.size());
	out_buffer->push_back(request->m_fe_tld_hint.size());
	out_buffer->end()[0] = '|';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), request->m_query.data(), request->m_query.size());
	out_buffer->push_back(request->m_query.size());
	out_buffer->end()[0] = '\n';
	out_buffer->push_back(1);
}

// en=100|da=60|no=60
void QueryLanguage::processResponse(fxclient_request_ptr_t base_request, char *response) {
	std::shared_ptr<QueryLanguageRequest> request = std::dynamic_pointer_cast<QueryLanguageRequest>(base_request);
	logTrace(g_conf.m_logTraceQueryLanguage, "Got result='%s' for query='%s'", response, request->m_query.c_str());

	std::vector<std::pair<lang_t, int>> languages;

	//parse the response
	auto tokens = split(response, '|');
	for (const auto &token : tokens) {
		auto pairs = split(token, '=');
		if (pairs.size() != 2) {
			// bad format
			continue;
		}

		lang_t language = getLangIdFromAbbr(pairs[0].c_str());
		if (language == langUnknown || language == langUnwanted) {
			// unknown / unwanted languages
			continue;
		}

		languages.emplace_back(language, static_cast<int>(strtoul(pairs[1].c_str(), nullptr, 0)));
	}

	// return default value if empty
	if (languages.empty()) {
		languages.emplace_back(langUnknown, 100);
	}

	(request->m_callback)(request->m_context, languages);
}

void QueryLanguage::errorCallback(fxclient_request_ptr_t base_request) {
	std::shared_ptr<QueryLanguageRequest> request = std::dynamic_pointer_cast<QueryLanguageRequest>(base_request);
	request->m_callback(request->m_context, std::vector<std::pair<lang_t, int>>());
}

bool QueryLanguage::initialize() {
	return FxClient::initialize("query language", "qlang", g_conf.m_queryLanguageServerName, g_conf.m_queryLanguageServerPort,
	                            g_conf.m_maxOutstandingQueryLanguage, g_conf.m_logTraceQueryLanguage);
}

bool QueryLanguage::getLanguage(void *context, query_language_callback_t callback,
                                const std::string &qlang_hint, const std::string &blang_hint,
                                const std::string &country_hint, const std::string &fe_tld_hint,
                                const std::string &query) {
	return sendRequest(std::static_pointer_cast<FxClientRequest>(std::make_shared<QueryLanguageRequest>(context, g_conf.m_queryLanguageTimeout, callback,
	                                                                                                    qlang_hint, blang_hint, country_hint, fe_tld_hint, query)));
}
