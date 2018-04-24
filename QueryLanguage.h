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
#ifndef FX_QUERYLANGUAGE_H
#define FX_QUERYLANGUAGE_H

#include "FxClient.h"
#include "Lang.h"

typedef void (*query_language_callback_t)(void *context, const std::vector<std::pair<lang_t, int>> &languages);

class QueryLanguage : public FxClient {
public:
	bool initialize();
	void reinitializeSettings();

	using FxClient::finalize;

	void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) override;
	void processResponse(fxclient_request_ptr_t base_request, char *response) override;
	void errorCallback(fxclient_request_ptr_t base_request) override;

	bool getLanguage(void *context, query_language_callback_t callback,
	                 const std::string &qlang_hint, const std::string &blang_hint,
	                 const std::string &country_hint, const std::string &fe_tld_hint,
	                 const std::string &query);

};

extern QueryLanguage g_queryLanguage;


#endif //FX_QUERYLANGUAGE_H
