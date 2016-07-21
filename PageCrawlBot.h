#ifndef GB_PAGECRAWLBOT_H
#define GB_PAGECRAWLBOT_H

class SafeBuf;
class CollectionRec;

bool printCrawlDetails2( SafeBuf *sb, CollectionRec *cx, char format );

bool getSpiderRequestMetaList ( const char *doc, SafeBuf *listBuf, bool spiderLinks, CollectionRec *cr);

#endif // GB_PAGECRAWLBOT_H
