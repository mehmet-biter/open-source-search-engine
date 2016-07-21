#ifndef GB_PAGECRAWLBOT_H
#define GB_PAGECRAWLBOT_H

class SafeBuf;
class CollectionRec;

bool printCrawlDetails2( SafeBuf *sb, CollectionRec *cx, char format );

bool printCrawlDetailsInJson ( SafeBuf *sb, CollectionRec *cx ) ;

bool printCrawlDetailsInJson ( SafeBuf *sb, CollectionRec *cx, int version ) ;


bool getSpiderRequestMetaList ( const char *doc, SafeBuf *listBuf, bool spiderLinks, CollectionRec *cr);

#endif // GB_PAGECRAWLBOT_H
