#ifndef GB_TITLERECVERSION_H
#define GB_TITLERECVERSION_H

#ifndef STRINGIFY
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)
#endif

// Starting version when Gigablast was open-sourced
//#define TITLEREC_CURRENT_VERSION 120

// better parsing of <script> tags
//#define TITLEREC_CURRENT_VERSION 121

// BR 20160106. New version that eliminates values in posdb that we do not need.
// we also stop decoding &amp; &gt; &lt; to avoid losing information
//#define TITLEREC_CURRENT_VERSION  122

// normalize url encoded url (url encode, strip params)
//#define TITLEREC_CURRENT_VERSION    123

// strip more parameters
//#define TITLEREC_CURRENT_VERSION    124

// strip ascii tab & newline from url
// store m_indexCode in TitleRec
//#define TITLEREC_CURRENT_VERSION    125

// new adult detection
//#define TITLEREC_CURRENT_VERSION    126

// handle robots meta with noindex, follow
//#define TITLEREC_CURRENT_VERSION    127

// explicit keywords field
//#define TITLEREC_CURRENT_VERSION    128

// make sure parameter is stripped even if query parameter is separated with '?'
#define TITLEREC_CURRENT_VERSION    129

#define TITLEREC_CURRENT_VERSION_STR    TO_STRING(TITLEREC_CURRENT_VERSION)

#endif // GB_TITLERECVERSION_H
