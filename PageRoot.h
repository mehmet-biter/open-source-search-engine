#ifndef GB_PAGEROOT_H
#define GB_PAGEROOT_H

#include "SafeBuf.h"

class SearchInput;
class CollectionRec;
class HttpRequest;

bool printFrontPageShell ( SafeBuf *sb,
                           const char *tabName,
                           CollectionRec *cr,
                           bool printGigablast );


bool expandHtml (  SafeBuf& sb,
		   const char *head , 
		   int32_t hlen ,
		   const char *q    , 
		   int32_t qlen ,
		   HttpRequest *r ,
		   SearchInput *si,
		   const char *method ,
		   CollectionRec *cr );

bool printLeftColumnRocketAndTabs ( SafeBuf *sb , 
				    bool isSearchResultsPage ,
				    CollectionRec *cr ,
				    const char *tabName );

#endif // GB_PAGEROOT_H
