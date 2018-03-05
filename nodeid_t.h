#ifndef NODEID_T_H__
#define NODEID_T_H__

#include <inttypes.h>

typedef int16_t nodeid_t;
//it really ought to be the tag enumeration from XmlNode.h

// this bit is used for indicating an end tag
#define BACKBIT     ((nodeid_t)0x8000)
#define BACKBITCOMP ((nodeid_t)0x7fff)


#endif
