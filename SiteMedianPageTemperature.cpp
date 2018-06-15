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
#include "SiteMedianPageTemperature.h"
#include "Conf.h"
#include "GbUtil.h"

// The protocol is very simple.
// The server Receives queries in the form
//	<query-id>:v1|sitehash<NL>
//
// The server responses:
//	<query-id>:site_median_page_temperature<NL>

SiteMedianPageTemperature g_siteMedianPageTemperature;

struct SiteMedianPageTemperatureRequest : public FxClientRequest {
	SiteMedianPageTemperatureRequest(void *context, int timeout_ms, site_median_page_temperature_callback_t callback, unsigned sitehash)
		: FxClientRequest(context, timeout_ms)
		, m_callback(callback)
		, m_sitehash(sitehash) {
	}

	site_median_page_temperature_callback_t m_callback;
	unsigned m_sitehash;
};

// v1|sitehash
void SiteMedianPageTemperature::convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) {
	std::shared_ptr<SiteMedianPageTemperatureRequest> request = std::dynamic_pointer_cast<SiteMedianPageTemperatureRequest>(base_request);

	out_buffer->reserve_extra(8 + 1 + 3 + 8 + 1);

	sprintf(out_buffer->end(), "%08x", seq);
	out_buffer->push_back(8);
	out_buffer->end()[0] = ':';
	out_buffer->push_back(1);

	memcpy(out_buffer->end(), "v1|", 3);
	out_buffer->push_back(3);

	sprintf(out_buffer->end(), "%08x", request->m_sitehash);
	out_buffer->push_back(8);
	out_buffer->end()[0] = '\n';
	out_buffer->push_back(1);
}

void SiteMedianPageTemperature::processResponse(fxclient_request_ptr_t base_request, char *response) {
	std::shared_ptr<SiteMedianPageTemperatureRequest> request = std::dynamic_pointer_cast<SiteMedianPageTemperatureRequest>(base_request);
	logTrace(g_conf.m_logTraceSiteMedianPageTemperature, "Got result='%s' for sitehash=%d", response, request->m_sitehash);

	unsigned long site_num_inlinks = strtoul(response, nullptr, 10);
	(request->m_callback)(request->m_context, site_num_inlinks);
}

void SiteMedianPageTemperature::errorCallback(fxclient_request_ptr_t base_request) {
	std::shared_ptr<SiteMedianPageTemperatureRequest> request = std::dynamic_pointer_cast<SiteMedianPageTemperatureRequest>(base_request);
	request->m_callback(request->m_context, {});
}

bool SiteMedianPageTemperature::initialize() {
	return FxClient::initialize("site temperature", "sitetemp", g_conf.m_siteMedianPageTemperatureServerName, g_conf.m_siteMedianPageTemperatureServerPort,
	                            g_conf.m_maxOutstandingSiteMedianPageTemperature, g_conf.m_logTraceSiteMedianPageTemperature);
}

void SiteMedianPageTemperature::reinitializeSettings() {
	FxClient::reinitializeSettings(g_conf.m_siteMedianPageTemperatureServerName, g_conf.m_siteMedianPageTemperatureServerPort,
	                               g_conf.m_maxOutstandingSiteMedianPageTemperature, g_conf.m_logTraceSiteMedianPageTemperature);
}

bool SiteMedianPageTemperature::getSiteMedianPageTemperature(void *context, site_median_page_temperature_callback_t callback, unsigned sitehash) {
	return sendRequest(std::static_pointer_cast<FxClientRequest>(std::make_shared<SiteMedianPageTemperatureRequest>(context, g_conf.m_siteMedianPageTemperatureTimeout, callback, sitehash)));
}
