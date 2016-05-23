#include "Wiki.h"

#include "Query.h"
#include "Words.h"
#include "Titledb.h"
#include "max_niceness.h"

// the global instance
Wiki g_wiki;

Wiki::Wiki () {
	m_callback = NULL;
	m_state    = NULL;
	m_opened   = false;
	// use a 4 byte key size and 1 byte data size
	m_ht.set ( 4 , 1,0,NULL,0,false,0 ,"tbl-wiki"); 
}

void Wiki::reset() {
	m_ht.reset();
}

Wiki::~Wiki () {
	if ( m_opened ) m_f.close();
}

// . load from disk
// . wikititles.txt (loads wikititles.dat if and date is newer)
bool Wiki::load() {

	// load it from .dat file if exists and is newer
	char ff1[256];
	char ff2[256];
	sprintf(ff1, "%swikititles.txt.part2", g_hostdb.m_dir);
	sprintf(ff2, "%swikititles2.dat", g_hostdb.m_dir);
	int fd1 = open ( ff1 , O_RDONLY );
	if ( fd1 < 0 ) log(LOG_INFO,"wiki: open %s: %s",ff1,mstrerror(errno));
	int fd2 = open ( ff2 , O_RDONLY );
	if ( fd2 < 0 ) log(LOG_INFO,"wiki: open %s: %s",ff2,mstrerror(errno));
	struct stat stats1;
	struct stat stats2;
	int32_t errno1 = 0;
	int32_t errno2 = 0;
	if ( fstat ( fd1 , &stats1 ) == -1 ) errno1 = errno;
	if ( fstat ( fd2 , &stats2 ) == -1 ) errno2 = errno;
	// close all
	close ( fd1 );
	close ( fd2 );

	// just use the .dat if we got it
	if ( ! errno2 ) {
		log(LOG_INFO,"wiki: Loading %s",ff2);
		// "dir" is NULL since already included in ff2
		return m_ht.load ( NULL , ff2 );
	}

	// if no text file that is bad
	if ( errno1 ) { 
		g_errno = errno1 ; 
		return log ("gb: could not open %s for reading: %s",ff1,
			    mstrerror(g_errno));
	}
	// get the size of it
	int32_t size = stats1.st_size;
	// now we have to load the text file
	return loadText( size );
}

bool Wiki::loadText ( int32_t fileSize ) {

	log(LOG_INFO,"wiki: generating wikititles2.dat file");

	SafeBuf sb;
	char ff1[256];
	sprintf(ff1, "%swikititles.txt.part1", g_hostdb.m_dir);
	log(LOG_INFO,"wiki: Loading %s",ff1);
	if ( ! sb.fillFromFile(ff1) ) return false;

	char ff2[256];
	sprintf(ff2, "%swikititles.txt.part2", g_hostdb.m_dir);
	log(LOG_INFO,"wiki: Loading %s",ff2);
	if ( ! sb.catFile(ff2) ) return false;

	sb.pushChar('\0');
	// should not have reallocated too much
	if ( sb.length() + 100 < sb.m_capacity ) { char *xx=NULL;*xx=0; }

	char *buf = sb.getBufStart();
	int32_t size = sb.length() - 1;

	// scan each line
	char *p    = buf;
	char *pend = buf + size;
	char *eol  = NULL;
	for ( ; p < pend ; p = eol + 1 ) {
		// skip spaces
		while ( p < pend && is_wspace_a ( *p ) ) p++;
		// find end of line, "eol" (also treat '(' as \n now)
		//for(eol = p; eol < pend && *eol !='\n' && *eol!='('; eol++) ;
		// do not use '(' since too many non-phraes in ()'s (for love)
		for (eol = p; eol < pend && *eol !='\n' ; eol++) ;
		// parse into words
		Words w;
		if ( !w.set( p, eol - p, true, MAX_NICENESS ) ) {
			return false;
		}

		int32_t nw = w.getNumWords();

		// skip if it begins with 'the', like 'the uk' because it
		// is causing uk to get a low score in 'boots in the uk'.
		// same for all stop words i guess...
		int32_t start = 0;

		// if no words, bail
		if ( start >= nw ) continue;
		// remove last words if not alnum
		if ( nw > 0 && !w.isAlnum(nw-1) ) nw--;
		// if no words, bail
		if ( start >= nw ) continue;
		// skip this line if no words
		if ( nw <= 0 ) continue;

		// skip if it has ('s in it
		char c = *eol;
		*eol = '\0';
		char *pp = NULL;
		if ( !pp ) pp = strstr ( p,"[" );
		if ( !pp && strncasecmp( p,"List of ",8)==0) pp = p;
		if ( !pp ) pp = strstr ( p,"," );
		// show it for debug
		//if ( ! pp ) printf("%s\n",p);
		*eol = c;
		if ( pp ) continue;
		// get these
		int64_t *wids = w.getWordIds();
		// reset hash
		uint32_t h = 0;
		// count the words in the phrase
		int32_t count = 0;
		// hash the word ids together
		for ( int32_t i = start ; i < nw ; i++ ) {
			// skip if not a proper word
			if ( ! w.isAlnum(i) ) continue;
			// add into hash quickly
			h = hash32Fast ( wids[i] & 0xffffffff , h );
			// count them
			count++;
		}
		// skip if too big
		if ( count > 250 ) continue;
		// store into hash table
	        // make negative i guess to indicate it is not
		// the best title form
		//if ( flag ) count = count * -1;
		if ( ! m_ht.addKey ( &h , &count ) ) return false;
	}

	// do not save if we can't
	if ( g_conf.m_readOnlyMode ) return true;
	// now save this hash table for quicker loading next time
	//char ff2[256];
	//sprintf(ff2, "%s/wikititles2.dat", g_hostdb.m_dir);
	if ( ! m_ht.save ( g_hostdb.m_dir , "wikititles2.dat" ) ) return false;

	log(LOG_INFO,"wiki: done generating wikititles2.dat file");

	// success
	return true;
}

// if a phrase in a query is in a wikipedia title, then increase
// its affWeights beyond the normal 1.0
int32_t Wiki::getNumWordsInWikiPhrase ( int32_t i, const Words *w ) {
	const int64_t *wids = w->getWordIds();
	if ( ! wids[i] ) return 0;
	int32_t nw = w->getNumWords();
	const char * const *wptrs = w->getWords();
	const int32_t  *wlens = w->getWordLens();
	// how many in the phrase
	int32_t max = -1;
	int32_t maxCount = 0;
	// accumulate a hash of the word ids
	//int64_t h      = 0LL;
	uint32_t h = 0;
	int32_t      wcount = 0;
	// otherwise, increase affinity high for included words
	for ( int32_t j = i ; j < nw && j < i + 12 ; j++ ) {
		// count all words
		wcount++;
		// skip if not alnum
		if ( ! wids[j] ) continue;
		// add to hash
		//h = hash64 ( wids[j] , h );
		// add into hash quickly
		h = hash32Fast ( wids[j] & 0xffffffff , h );
		// skip single words, we only want to check phrases
		if ( j == i ) continue;
		// look in table
		char *vp = (char *)m_ht.getValue ( &h );
		// skip if nothing
		if ( ! vp ) {
			// try combining. FIX FOR "Lock_pick". we want that to
			// be a wikipedia phrase, but it's not recorded because
			// its case is mixed.
			if ( j != i + 2 ) continue;
			// fix for "Make a" being a phrase because "Makea"
			// is in the wikipedia. fix for
			// 'how to make a lock pick set'
			if ( wlens[i+2] <= 2 ) continue;
			// special hash
			uint64_t h64 = 0;
			int32_t conti = 0;
			// add into hash quickly
			h64 = hash64Lower_utf8_cont(wptrs[i], 
						    wlens[i],
						    h64,
						    &conti );
			h64 = hash64Lower_utf8_cont(wptrs[i+2], 
						    wlens[i+2],
						    h64,
						    &conti );
			// try looking that up
			uint32_t hf32 = h64 & 0xffffffff;
			vp = (char *)m_ht.getValue(&hf32);
		}
		if ( ! vp ) continue;
		// we got a match
		max = j;
		maxCount = wcount;
	}
	// return now if we got one
	if ( maxCount > 0 ) return maxCount;
	// otherwise, try combining so "lock pick" is a wikipedia phrase because
	// "lockpick" is a wikipedia title.


	return maxCount;
}

