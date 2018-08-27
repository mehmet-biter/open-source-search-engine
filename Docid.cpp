#include "Docid.h"
#include "Url.h"
#include "hash.h"
#include "Titledb.h" //DOCID_MASK



static uint64_t getProbableDocId(const char *url, const char *dom, int32_t domLen) {
	uint64_t probableDocId = hash64b(url,0) & DOCID_MASK;
	// clear bits 6-13 because we want to put the domain hash there
	// dddddddd dddddddd ddhhhhhh hhdddddd
	probableDocId &= 0xffffffffffffc03fULL;
	uint32_t h = hash8(dom,domLen);
	//shift the hash by 6
	h <<= 6;
	// OR in the hash
	probableDocId |= h;
	return probableDocId;
}

uint64_t Docid::getProbableDocId(const Url *url) {
	return ::getProbableDocId(url->getUrl(), url->getDomain(), url->getDomainLen());
}


uint64_t Docid::getProbableDocId(const char *url) {
	Url u;
	u.set( url );
	return getProbableDocId(&u);
}



uint64_t Docid::getFirstProbableDocId(int64_t d) {
	return d & 0xffffffffffffffc0ULL;
}

uint64_t Docid::getLastProbableDocId(int64_t d) {
	return d | 0x000000000000003fULL;
}


uint8_t Docid::getDomHash8FromDocId (int64_t d) {
	return (d & ~0xffffffffffffc03fULL) >> 6;
}
