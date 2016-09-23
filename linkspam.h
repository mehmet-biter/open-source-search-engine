// Matt Wells, copyright Nov 2001

#ifndef GB_LINKSPAM_H
#define GB_LINKSPAM_H

#include "gb-include.h"
#include "ip.h"

class Url;

bool setLinkSpam ( int32_t             ip                 ,
                   const Url       *linker             ,
                   int32_t             siteNumInlinks     ,
		   class Xml       *xml                ,
		   class Links     *links              ,
		   bool             isContentTruncated );

bool isLinkSpam  ( const  Url       *linker         ,
		   int32_t             ip             ,
		   int32_t             siteNumInlinks ,
		   class Xml       *xml            ,
		   class Links     *links          ,
		   int32_t             maxDocLen      , 
		   const char           **note           ,
		   const Url          *linkee         ,
		   int32_t             linkNode       );

#endif // GB_LINKSPAM_H
