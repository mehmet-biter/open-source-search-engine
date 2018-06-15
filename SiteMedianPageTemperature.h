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
#ifndef FX_SITEMEDIANPAGETEMPERATURE_H
#define FX_SITEMEDIANPAGETEMPERATURE_H

#include "FxClient.h"

typedef void (*site_median_page_temperature_callback_t)(void *context, long count);

class SiteMedianPageTemperature : public FxClient {
public:
	bool initialize();
	void reinitializeSettings();

	using FxClient::finalize;

	void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) override;
	void processResponse(fxclient_request_ptr_t base_request, char *response) override;
	void errorCallback(fxclient_request_ptr_t base_request) override;

	bool getSiteMedianPageTemperature(void *context, site_median_page_temperature_callback_t callback, unsigned sitehash);
};

extern SiteMedianPageTemperature g_siteMedianPageTemperature;


#endif //FX_SITEMEDIANPAGETEMPERATURE_H
