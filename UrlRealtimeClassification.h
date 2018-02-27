#ifndef URL_REALTIME_CLASSIFICATION_H_
#define URL_REALTIME_CLASSIFICATION_H_

#include <inttypes.h>
#include "FxClient.h"

// Functions for classifying URLs in realtime. Mostly used for marking URLs as
// phising/malware, but could also be used for adult sites etc.


// bitmasks for classification
#define URL_CLASSIFICATION_MALICIOUS 0x00000001 //malware, phising, trojan, extortion, ...
#define URL_CLASSIFICATION_UNKNOWN   0x10000000 //classification failed

//Classify an url. Returns true if classification has been initated and callback
//will eventually be called. Returns false if classification has not been initated.
typedef void (*url_realtime_classification_callback_t)(void *context, uint32_t classification);

class UrlRealtimeClassification : public FxClient {
public:
	bool initialize();
	using FxClient::finalize;

	void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) override;
	void processResponse(fxclient_request_ptr_t base_request, char *response) override;
	void errorCallback(fxclient_request_ptr_t base_request) override;

	bool classifyUrl(const char *url, url_realtime_classification_callback_t callback, void *context);
	bool realtimeUrlClassificationWorks();
};

extern UrlRealtimeClassification g_urlRealtimeClassification;

#endif
