#ifndef PAGEROOT_H_
#define PAGEROOT_H_

#include "SafeBuf.h"
#include "Collectiondb.h"

bool printFrontPageShell ( SafeBuf *sb,
                           char *tabName,
                           CollectionRec *cr,
                           bool printGigablast );

#endif
