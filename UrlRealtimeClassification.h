#ifndef URL_REALTIME_CLASSIFICATION_H_
#define URL_REALTIME_CLASSIFICATION_H_

#include <inttypes.h>

// Functions for classifying URLs in realtime. Mostly used for marking URLs as
// phising/malware, but could also be used for adult sites etc.


// bitmasks for classification
#define URL_CLASSIFICATION_MALICIOUS 0x00000001 //malware, phising, trojan, extortion, ...
#define URL_CLASSIFICATION_UNKNOWN   0x10000000 //classification failed


//Classify an url. Returns true if classification has been initated and callback
//will eventually be called. Returns false if classification has not been initated.
typedef void (*url_realtime_classification_callback_t)(void *context, uint32_t classification);
bool classifyUrl(const char *url, url_realtime_classification_callback_t callback, void *context);

bool initializeRealtimeUrlClassification();
void finalizeRealtimeUrlClassification();

#endif
