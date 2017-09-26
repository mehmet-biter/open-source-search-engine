#ifndef COLLECTION_SPIDER_STATUS_T_H_
#define COLLECTION_SPIDER_STATUS_T_H_

// . values for CollectionRec::m_spiderStatus
// . reasons why crawl is not happening
enum class spider_status_t : char {
	SP_INITIALIZING  = 0,
	SP_PAUSED        = 6,   // user paused spider
	SP_INPROGRESS    = 7,   // it is going on!
	SP_ADMIN_PAUSED  = 8,   // g_conf.m_spideringEnabled = false
};


#endif
