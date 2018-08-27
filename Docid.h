#ifndef DOCID_H_
#define DOCID_H_
#include <inttypes.h>


class Url;

//Docids are based on hashes of the URL. In case of collisions we chose docid in the same docid-shard-range
//so the actual docid will be on the same shard.

namespace Docid {

//URL -> probably docid
uint64_t getProbableDocId(const Url *url);
uint64_t getProbableDocId(const char *url);

//probable-docid -> docid-shard-range lower bound
uint64_t getFirstProbableDocId(int64_t d);
//probable-docid -> docid-shard-range upper bound
uint64_t getLastProbableDocId(int64_t d);

//an 8-bit hash of the domain is also in the docid
uint8_t getDomHash8FromDocId (int64_t d);


} //namespace


#endif
