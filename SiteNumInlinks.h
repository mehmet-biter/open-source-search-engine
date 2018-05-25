//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
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
#ifndef FX_SITENUMINLINKS_H
#define FX_SITENUMINLINKS_H

#include "FxClient.h"

typedef void (*site_inlinks_count_callback_t)(void *context, long count);

class SiteNumInlinks : public FxClient {
public:
	bool initialize();
	void reinitializeSettings();

	using FxClient::finalize;

	void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) override;
	void processResponse(fxclient_request_ptr_t base_request, char *response) override;
	void errorCallback(fxclient_request_ptr_t base_request) override;

	bool getSiteNumInlinks(void *context, site_inlinks_count_callback_t callback, unsigned sitehash);
};

extern SiteNumInlinks g_siteNumInlinks;


#endif //FX_SITENUMINLINKS_H
