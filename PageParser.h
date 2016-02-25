#ifndef _PAGEPARSER_H_
#define _PAGEPARSER_H_

// global flag
extern bool g_inPageParser ;
extern bool g_inPageInject ;

#define PP_NICENESS 2

#include "XmlDoc.h"
#include "Pages.h"
#include "Unicode.h"
#include "Title.h"
#include "Pos.h"
#include "TopTree.h"

bool sendPageAnalyze ( TcpSocket *s , HttpRequest *r ) ;

#endif
