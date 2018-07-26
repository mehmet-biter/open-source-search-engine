#include "UrlRealtimeClassification.h"
#include "Conf.h"
#include "Log.h"

// The protocol is very simple.
// The server Receives queries in the form
//	<query-id>:<url><NL>
// (The client must escape-encode the URL)
//
// The server responses:
//	<query-id>:[<flag>['+'<flag>...]]<NL>

UrlRealtimeClassification g_urlRealtimeClassification;

struct UrlRealtimeClassificationRequest : public FxClientRequest {
	UrlRealtimeClassificationRequest(void *context, int timeout_ms, url_realtime_classification_callback_t callback, const std::string &url)
		: FxClientRequest(context, timeout_ms)
		, m_callback(callback)
		, m_url(url) {
	}

	url_realtime_classification_callback_t m_callback;
	std::string m_url;
};

void UrlRealtimeClassification::convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) {
	std::shared_ptr<UrlRealtimeClassificationRequest> request = std::dynamic_pointer_cast<UrlRealtimeClassificationRequest>(base_request);

	out_buffer->reserve_extra(8 + 1 + request->m_url.size() + 1);
	sprintf(out_buffer->end(), "%08x", seq);
	out_buffer->push_back(8);
	out_buffer->end()[0] = ':';
	out_buffer->push_back(1);
	memcpy(out_buffer->end(), request->m_url.data(), request->m_url.size());
	out_buffer->push_back(request->m_url.size());
	out_buffer->end()[0] = '\n';
	out_buffer->push_back(1);
}

void UrlRealtimeClassification::processResponse(fxclient_request_ptr_t base_request, char *response) {
	char *ss = nullptr;
	uint32_t classification = 0;

	//parse the [flag[+flag...]]
	for (const char *s = strtok_r(response, "+", &ss); s; s = strtok_r(nullptr, "+", &ss)) {
		if (strcmp(s, "MALWARE") == 0) {
			classification |= URL_CLASSIFICATION_MALICIOUS;
		}
		//todo: else ... other classifications
	}

	std::shared_ptr<UrlRealtimeClassificationRequest> request = std::dynamic_pointer_cast<UrlRealtimeClassificationRequest>(base_request);

	logTrace(g_conf.m_logTraceUrlClassification, "Got classification %08x for %s", classification, request->m_url.c_str());

	(request->m_callback)(request->m_context, classification);
}

void UrlRealtimeClassification::errorCallback(fxclient_request_ptr_t base_request) {
	std::shared_ptr<UrlRealtimeClassificationRequest> request = std::dynamic_pointer_cast<UrlRealtimeClassificationRequest>(base_request);
	request->m_callback(request->m_context, URL_CLASSIFICATION_UNKNOWN);
}

bool UrlRealtimeClassification::initialize() {
	return FxClient::initialize("url classification", "urlclass", g_conf.m_urlClassificationServerName, g_conf.m_urlClassificationServerPort,
	                            g_conf.m_maxOutstandingUrlClassifications, g_conf.m_logTraceUrlClassification);
}


bool UrlRealtimeClassification::classifyUrl(const char *url, url_realtime_classification_callback_t callback, void *context) {
	return sendRequest(std::static_pointer_cast<FxClientRequest>(std::make_shared<UrlRealtimeClassificationRequest>(context, g_conf.m_urlClassificationTimeout, callback, url)));
}


bool UrlRealtimeClassification::realtimeUrlClassificationWorks() {
	return communicationWorks();
}
