// print events should print &nbsp; if nothing else to print

// when a div tag's parent truncates its section, it may have been
// paired up with a div back tag which then should become free...
// that is the problem... because those back tags are unpaired.
// so your parent should constrain you as SOON as it is constrained and
// close you up at that point. that way you cannot falsely pair-claim
// a div back tag.


#include "Sections.h"
#include "Url.h"
#include "Words.h"
#include "Msg40.h"
#include "Conf.h"
#include "Msg1.h" // getGroupId()
#include "XmlDoc.h"
#include "Bits.h"
#include "sort.h"
#include "Abbreviations.h"

Sections::Sections ( ) {
	m_sections = NULL;
	m_buf      = NULL;
	m_buf2     = NULL;
	reset();
}

void Sections::reset() {
	//if ( m_sections && m_needsFree )
	//	mfree ( m_sections , m_sectionsBufSize , "Sections" );
	m_sectionBuf.purge();
	m_sectionPtrBuf.purge();
	if ( m_buf && m_bufSize )
		mfree ( m_buf , m_bufSize , "sdata" );
	if ( m_buf2 && m_bufSize2 )
		mfree ( m_buf2 , m_bufSize2 , "sdata2" );

	m_sections         = NULL;
	m_buf              = NULL;
	m_buf2             = NULL;
	m_bits             = NULL;
	m_numSections      = 0;
	m_numSentenceSections = 0;
	m_badHtml          = false;
	m_sentFlagsAreSet  = false;
	m_addedImpliedSections = false;
	m_rootSection      = NULL;
	m_lastSection      = NULL;
	m_lastAdded        = NULL;

	m_numLineWaiters   = 0;
	m_waitInLine       = false;
	m_hadArticle       = false;
	m_articleStartWord = -2;
	m_articleEndWord   = -2;
	m_recall           = 0;
	//m_totalSimilarLayouts = 0;
	m_numVotes = 0;
	m_nw = 0;
	m_firstSent = NULL;
	m_lastSent  = NULL;
	m_sectionPtrs = NULL;
	m_alnumPosValid = false;
}

Sections::~Sections ( ) {
	reset();
}

// for debug watch point
class Sections *g_sections = NULL;
class Section *g_sec = NULL;

#define TXF_MATCHED 1

// an element on the stack is a Tag
class Tagx {
public:
	// id of the fron tag we pushed
	nodeid_t m_tid;
	// cumulative hash of all tag ids containing this one, includes us
	//int32_t     m_cumHash;
	// section number we represent
	int32_t     m_secNum;
	// hash of all the alnum words in this section
	//int32_t     m_contentHash;
	// set to TXF_MATCHED
	char     m_flags;
};

// i lowered from 1000 to 300 so that we more sensitive to malformed pages
// because typically they seem to take longer to parse. i also added some
// new logic for dealing with table tr and td back tags that allow us to
// pop off the other contained tags right away rather than delaying it until
// we are done because that will often breach this stack.
#define MAXTAGSTACK 300

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
// . sets m_sections[] array, 1-1 with words array "w"
// . the Weights class can look at these sections and zero out the weights
//   for words in script, style, select and marquee sections
bool Sections::set( Words *w, Bits *bits, Url *url, int64_t siteHash64,
					char *coll, int32_t niceness, uint8_t contentType ) {
	reset();

	if ( ! w ) return true;

	if ( w->m_numWords > 1000000 ) {
		log("sections: over 1M words. skipping sections set for "
		    "performance.");
		return true;
	}

	// save it
	m_words           = w;
	m_bits            = bits;
	m_url             = url;
	m_siteHash64      = siteHash64;
	m_coll            = coll;
	m_niceness        = niceness;
	m_contentType     = contentType;

	// reset this just in case
	g_errno = 0;

	if ( w->getNumWords() <= 0 ) return true;

	// shortcuts
	int64_t   *wids  = w->getWordIds  ();
	nodeid_t    *tids  = w->getTagIds   ();
	int32_t           nw  = w->getNumWords ();
	char      **wptrs  = w->getWords    ();
	int32_t        *wlens = w->getWordLens ();

	// set these up for isDelimeter() function to use and for
	// isCompatible() as well
	m_wids  = wids;
	m_wlens = wlens;
	m_wptrs = wptrs;
	m_tids  = tids;

	m_isRSSExt = false;
	char *ext = m_url->getExtension();
	if ( ext && strcasecmp(ext,"rss") == 0 ) m_isRSSExt = true;
	if ( m_contentType == CT_XML ) m_isRSSExt = true;

	// are we a trumba.com url? we allow colons in sentences for its
	// event titles so that we get the correct event title. fixes
	// tumba.com "Guided Nature Walk : ..." title
	char *dom  = m_url->getDomain();
	int32_t  dlen = m_url->getDomainLen();
	m_isFacebook   = false;
	m_isEventBrite = false;
	m_isStubHub    = false;
	if ( dlen == 12 && strncmp ( dom , "facebook.com" , 12 ) == 0 )
		m_isFacebook = true;
	if ( dlen == 11 && strncmp ( dom , "stubhub.com" , 11 ) == 0 )
		m_isStubHub = true;
	if ( dlen == 14 && strncmp ( dom , "eventbrite.com" , 14 ) == 0 )
		m_isEventBrite = true;

	// . how many sections do we have max?
	// . init at one to count the root section
	int32_t max = 1;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . count all front tags
		// . count twice since it can terminate a SEC_SENT sentence sec
		//if ( tids[i] && !(tids[i]&BACKBIT) ) max += 2;
		// count back tags too since some url 
		// http://www.tedxhz.com/tags.asp?id=3919&id2=494 had a bunch
		// of </p> tags with no front tags and it cored us because
		// m_numSections > m_maxNumSections!
		if ( tids[i] ) max += 2; // && !(tids[i]&BACKBIT) ) max += 2;
		// or any hr tag
		//else if ( tids[i] == (BACKBIT|TAG_HR) ) max += 2;
		//else if ( tids[i] == (BACKBIT|TAG_BR) ) max += 2;
		// . any punct tag could have a bullet in it...
		// . or if its a period could make a sentence section
		//else if ( ! tids[i] && ! wids[i] ) {
		else if ( ! wids[i] ) {
			// only do not count simple spaces
			if ( m_wlens[i] == 1 && is_wspace_a(m_wptrs[i][0]))
				continue;
			// otherwise count it as sentence delimeter
			max++;
		}
	}
	// . then \0 allows for a sentence too!
	// . fix doc that was just "localize-sf-prod\n"
	max++;
	// and each section may create a sentence section
	max *= 2;

	// truncate if excessive. growSections() will kick in then i guess
	// if we need more sections.
	if ( max > 1000000 ) {
		log("sections: truncating max sections to 1000000");
		max = 1000000;
	}

	//max += 5000;
	int32_t need = max * sizeof(Section);


	// and we need one section ptr for every word!
	//need += nw * 4;
	// and a section ptr for m_sorted[]
	//need += max * sizeof(Section *);
	// set this
	m_maxNumSections = max;

	// breathe
	QUICKPOLL(m_niceness);

	m_sectionPtrBuf.setLabel("psectbuf");

	// separate buf now for section ptr for each word
	if ( ! m_sectionPtrBuf.reserve ( nw *sizeof(Section *)) ) return true;
	m_sectionPtrs = (Section **)m_sectionPtrBuf.getBufStart();

	// allocate m_sectionBuf
	m_sections = NULL;

	m_sectionBuf.setLabel ( "sectbuf" );

	if ( ! m_sectionBuf.reserve ( need ) )
		return true;

	// point into it
	m_sections = (Section *)m_sectionBuf.getBufStart();

	// reset
	m_numAlnumWordsInArticle = 0;

	m_titleStart = -1;
	m_titleEnd   = -1;

	// save this too
	m_nw = nw;

	// stack of front tags we encounter
	Tagx stack[MAXTAGSTACK];
	Tagx *stackPtr = stack;

	Section *current     = NULL;
	Section *rootSection = NULL;

	// assume none
	m_rootSection = NULL;

	// only add root section if we got some words
	if ( nw > 0 ) {
		// record this i guess
		rootSection = &m_sections[m_numSections];
		// clear
		memset ( rootSection , 0 , sizeof(Section) );
		// . the current section we are in
		// . let's use a root section
		current = rootSection;
		// init that to be the whole page
		rootSection->m_b = nw;
		// save it
		m_rootSection = rootSection;
		// to fix a core dump
		rootSection->m_baseHash = 1;
		// advance
		m_numSections++;
	}

	// count this
	int32_t alnumCount = 0;
	// for debug
	g_sections = this;

	// Sections are no longer 1-1 with words, just with front tags
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// we got it
		//m_sectionPtrs[i] = current;
		nodeid_t fullTid = tids[i];
		// are we a non-tag?
		if ( ! fullTid ) { 
			// skip if not alnum word
			if ( ! wids[i] ) continue;
			// count it raw
			alnumCount++;
			// must be in a section at this point
			if ( ! current ) { char *xx=NULL;*xx=0; }
			// . hash it up for our content hash
			// . this only hashes words DIRECTLY in a 
			//   section because we can have a large menu
			//   section with a little bit of text, and we
			// contain some non-menu sections.
			//ch = hash32h ( (int32_t)wids[i] , ch );
			// if not in an anchor, script, etc. tag
			//if ( ! inFlag ) current->m_plain++;
			// inc count in current section
			current->m_exclusive++;
			continue;
		}

		// make a single section for input tags
		if ( fullTid == TAG_INPUT ||
		     fullTid == TAG_HR    ||
		     fullTid == TAG_COMMENT ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections && 
			     ! growSections() ) 
				return true;
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// init the flags of the section
			//sn->m_flags = inFlag ;
			// section consists of just this tag
			sn->m_b = i + 1;
			// go on to next
			continue;
		}

		// a section of multiple br tags in a sequence
		if ( fullTid == TAG_BR ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections && 
			     ! growSections() ) 
				return true;
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// init the flags of the section
			//sn->m_flags = inFlag ;
			// count em up
			int32_t brcnt = 1;
			// scan for whole sequence
			int32_t lastBrPos = i;
			for ( int32_t j = i + 1 ; j < nw ; j++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// claim br tags
				if ( tids[j] == TAG_BR ) { 
					lastBrPos = j;
					brcnt++; 
					continue; 
				}
				// break on words
				if ( wids[j] ) break;
				// all spaces is ok
				if ( w->isSpaces(j) ) continue;
				// otherwise, stop on other punct
				break;
			}
			// section consists of just this tag
			sn->m_b = lastBrPos + 1;
			// advance
			i = lastBrPos;
			// set this for later so that getDelimHash() returns
			// something different based on the br count for
			// METHOD_ATTRIBUTE
			sn->m_baseHash = 19999 + brcnt;
			// go on to next
			continue;
		}

		// get the tag id without the back bit
		nodeid_t tid = fullTid & BACKBITCOMP;

		// . ignore tags with no corresponding back tags
		// . if they have bad html and have front tags
		//   with no corresponding back tags, that will hurt!
		// . make exception for <li> tag!!!
		// . was messing up:
		//   http://events.kqed.org/events/index.php?com=detail&
		//   eID=9812&year=2009&month=11
		//   for parsing out events
		// . make excpetion for <p> tag too! most ppl use </p>
		if ( ( ! hasBackTag ( tid ) || 
		       wptrs[i][1] =='!'    || // <!ENTITY rdfns...>
		       wptrs[i][1] =='?'    ) &&
		     tid != TAG_P &&
		     tid != TAG_LI )
			continue;

		// . these imply no back tag
		// . <description />
		// . fixes inconsistency in 
		//   www.trumba.com/calendars/KRQE_Calendar.rss
		if ( wptrs[i][wlens[i]-2] == '/' && tid == TAG_XMLTAG )
			continue;

		// ignore it cuz we decided it was unbalanced
		//if ( bits->m_bits[i] & D_UNBAL ) continue;

		// and these often don't have back tags, like <objectid .> WTF!
		// no! we need these for the xml-based rss feeds
		// TODO: we should parse the whole doc up front and determine
		// which tags are missing back tags...
		//if ( tid == TAG_XMLTAG ) continue;

		// wtf, embed has no back tags. i fixed this in XmlNode.cpp
		//if ( tid == TAG_EMBED ) continue;
		// do not breach the stack
		if ( stackPtr - stack >= MAXTAGSTACK ) {
			log("html: stack breach for %s",url->getUrl());
			// if we set g_errno and return then the url just
			// ends up getting retried once the spider lock
			// in Spider.cpp expires in MAX_LOCK_AGE seconds.
			// about an hour. but really we should completely
			// give up on this. whereas we should retry OOM errors
			// etc. but this just means bad html really.
			//g_errno = ETAGBREACH;
			// just reset to 0 sections then
			reset();
			return true;
		}

		char gotBackTag ;
		if ( fullTid != tid ) gotBackTag = 1;
		else                  gotBackTag = 0;

		// "pop tid", tid to pop off stack
		nodeid_t ptid       = tid;
		nodeid_t fullPopTid = fullTid;

		// no nested <li> tags allowed
		if ( fullTid == TAG_LI &&  
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_LI ) 
			gotBackTag = 2;

		// no nested <b> tags allowed
		if ( fullTid == TAG_B &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_B ) 
			gotBackTag = 2;

		// no nested <a> tags allowed
		if ( fullTid == TAG_A &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_A ) 
			gotBackTag = 2;

		// no nested <p> tags allowed
		if ( fullTid == TAG_P &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_P ) 
			gotBackTag = 2;

		// no <hN> tags inside a <p> tag
		// fixes http://www.law.berkeley.edu/140.htm
		if ( fullTid >= TAG_H1 &&
		     fullTid <= TAG_H5 &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_P ) {
			// match this on stack
			ptid       = TAG_P;
			fullPopTid = TAG_P;
			gotBackTag = 2;
		}

		// no nested <td> tags allowed
		if ( fullTid == TAG_TD &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TD ) 
			gotBackTag = 2;

		// encountering <tr> when in a <td> closes the <td> AND
		// should also close the <tr>!!
		if ( fullTid == TAG_TR &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TD )
			gotBackTag = 2;

		// no nested <tr> tags allowed
		if ( fullTid == TAG_TR &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TR )
			gotBackTag = 2;

		// this is true if we are a BACK TAG
		if ( gotBackTag ) {

			// ignore span tags that are non-breaking because they
			// do not change the grouping/sectioning behavior of
			// the web page and are often abused.
			if ( ptid == TAG_SPAN ) continue;

			// fix for gwair.org
			if ( ptid == TAG_FONT ) continue;

			// too many people use these like a <br> tag or
			// make them open-ended or unbalanced
			//if ( tid == TAG_P      ) continue;
			if ( ptid == TAG_CENTER ) continue;
			
		subloop:
			// don't blow the stack
			if ( stackPtr == stack ) continue;

			// point to it
			Tagx *spp = (stackPtr - 1);

			// init it
			Tagx *p ;
			// scan through the stack until we find a
			// front tag that matches this back tag
			//for(p = spp ; p >= stack && gotBackTag == 1 ; p-- ) {
			for ( p = spp ; p >= stack ; p-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				// no match?
				if ( p->m_tid != ptid ) {
					// matched before? we can pop
					if ( p->m_flags & TXF_MATCHED )
						continue;
					// keep on going
					continue;
				}
				// do not double match
				if ( p->m_flags & TXF_MATCHED )
					continue;
				// flag it cuz we matched it
				p->m_flags |= TXF_MATCHED;
				// set the stack ptr to it
				spp = p;
				// and stop
				break;
			}

			// no matching front tag at all?
			// then just ignore this back tag
			if ( p < stack ) continue;

			// get section number of the front tag
			//int32_t xn = *(secNumPtr-1);
			int32_t xn = spp->m_secNum;
			// sanity
			if ( xn<0 || xn>=m_numSections ) {char*xx=NULL;*xx=0;}
			// get it
			Section *sn = &m_sections[xn];

			// we are now back in the parent section
			//current = sn;
			// record the word range of the secion we complete
			sn->m_b = i+1;

			// do not include the <li> tag as part of it
			// otherwise we end up with overlapping section since
			// this tag ALSO starts a section!!
			if ( gotBackTag == 2 ) sn->m_b = i;

			// if our parent got closed before "sn" closed because
			// it hit its back tag before we hit ours, then we
			// must cut ourselves short and try to match this
			// back tag to another front tag on the stack
			Section *ps = sn->m_parent;
			for ( ; ps != rootSection ; ps = ps->m_parent ) {
				// skip if parent no longer contains us!
				if ( ps->m_b <= sn->m_a ) continue;
				// skip if this parent is still open
				if ( ps->m_b <= 0 ) continue;
				// parent must have closed before us
				if ( ps->m_b > sn->m_b ) {char *xx=NULL;*xx=0;}
				// we had no matching tag, or it was unbalanced
				// but i do not know which...!
				sn->m_flags |= SEC_OPEN_ENDED;
				// cut our end shorter
				sn->m_b = ps->m_b;
				// our TXF_MATCHED bit should still be set
				// for spp->m_flags, so try to match ANOTHER
				// front tag with this back tag now
				if ( ! ( spp->m_flags & TXF_MATCHED ) ) {
					char *xx=NULL;*xx=0; }
				// ok, try to match this back tag with another
				// front tag on the stack, because the front
				// tag we had selected got cut short because
				// its parent forced it to cut short.
				goto subloop;
			}
   
			// sanity check
			if ( sn->m_b <= sn->m_a ) { char *xx=NULL;*xx=0;}

			// mark the section as unbalanced
			if ( spp != (stackPtr - 1) )
				sn->m_flags |= SEC_UNBALANCED;

			// revert it to this guy, may not equal stackPtr-1 !!
			stackPtr = spp;
			
			// get parent section
			if ( stackPtr > stack ) {
				// get parent section now
				//xn = *(secNumPtr-1);
				xn = (stackPtr-1)->m_secNum;
				// set current to that
				current = &m_sections[xn];
			}
			else {
				//current = NULL;
				//char *xx=NULL;*xx=0; 
				// i guess this is bad html!
				current = rootSection;
			}
			
			// debug log
			if ( g_conf.m_logDebugSections ) {
				char *ms = "";
				if ( stackPtr->m_tid != ptid) ms =" UNMATCHED";
				char *back ="";
				if ( fullPopTid & BACKBIT ) back = "/";
				logf(LOG_DEBUG,"section: pop tid=%"INT32" "
				     "i=%"INT32" "
				     "level=%"INT32" "
				     "%s%s "
				     //"h=0x%"XINT32""
				     "%s",(int32_t)tid,
				     i,
				     (int32_t)(stackPtr - stack),
				     back,g_nodes[tid].m_nodeName,
				     //h,
				     ms);
			}
			
			// . if we were a back tag, we are done... but if we
			//   were a front tag, we must add ourselves below...
			// . MDW: this seems more logical than the if-statement
			//        below...
			if ( fullTid != tid ) continue;
		}

		// ignore paragraph/center tags, too many people are careless
		// with them... santafeplayhouse.com
		// i can't ignore <p> tags anymore because for
		// http://www.abqfolkfest.org/resources.shtml we are allowing
		// "Halloween" to have "SEP-DEC" as a header even though
		// that header is BELOW "Halloween" just because we THINK
		// they are in the same section BUT in reality they were
		// in different <p> tags. AND now it seems that the 
		// improvements we made to Sections.cpp for closing open
		// ended tags are pretty solid that we can unignore <p>
		// tags again, it only helped the qa run...
		//if ( tid == TAG_P ) continue;
		if ( tid == TAG_CENTER ) continue;

		if ( tid == TAG_SPAN ) continue;
		// gwair.org has font tags the pair up a date "1st Sundays"
		// with the address above it, and it shouldn't do that!
		if ( tid == TAG_FONT ) continue;

		// try to realloc i guess. should keep ptrs in tact.
		if ( m_numSections >= m_maxNumSections && ! growSections() ) 
			return true;
		// get the section
		Section *sn = &m_sections[m_numSections];
		// clear
		memset ( sn , 0 , sizeof(Section) );
		// inc it
		m_numSections++;
		// sanity check - breach check
		if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
		
		// set our parent
		sn->m_parent = current;

		// set this
		current = sn;
		
		// need to keep a word range that the section covers
		sn->m_a = i;

		// assume no terminating bookend
		sn->m_b = -1;

		// push a unique id on the stack so we can pop if we
		// enter a subsection
		stackPtr->m_tid         = tid;
		//stackPtr->m_cumHash   = h;
		stackPtr->m_secNum      = m_numSections - 1;
		//stackPtr->m_contentHash = ch;
		stackPtr->m_flags       = 0;
		stackPtr++;

		// debug log
		if ( ! g_conf.m_logDebugSections ) continue;

		logf(LOG_DEBUG,"section: push tid=%"INT32" "
		     "i=%"INT32" "
		     "level=%"INT32" "
		     "%s "
		     ,
		     (int32_t)tid,
		     i,
		     (int32_t)(stackPtr - stack)-1,
		     g_nodes[(int32_t)tid].m_nodeName
		     );
	}

	// if first word in a section false outside of the parent section
	// then reparent to the grandparent. this can happen when we end
	// up closing a parent section before ???????
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *si = &m_sections[i];
		// skip if we are still open-ended
		if ( si->m_b < 0 ) continue;
		// get parent
		Section *sp = si->m_parent;
		// skip if no parent
		if ( ! sp ) continue;
		// skip if parent still open ended
		if ( sp->m_b < 0 ) continue;
		// subloop it
	doagain:
		// skip if no parent
		if ( ! sp ) continue;
		// parent must start before us
		if ( sp->m_a > si->m_a ) { char *xx=NULL;*xx=0; }
		// . does parent contain our first word?
		// . it need not fully contain our last word!!!
		if ( sp->m_a <= si->m_a && sp->m_b > si->m_a ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_a <= si->m_a && sp->m_b == -1 ) continue;
		// get grandparent
		sp = sp->m_parent;
		// set
		si->m_parent = sp;
		// try again
		goto doagain;
	}

	bool inFrame = false;
	int32_t gbFrameNum = 0;

	//
	// . set Section::m_xmlNameHash for xml tags here
	// . set Section::m_frameNum and SEC_IN_GBFRAME bit
	//
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// get it
		int32_t ws = sn->m_a;
		// shortcut
		nodeid_t tid = tids[ws];

		// set SEC_IN_FRAME
		if ( tid == TAG_GBFRAME ) {
			// start or end?
			gbFrameNum++;
			inFrame = true;
		}
		if ( tid == (TAG_GBFRAME | BACKBIT) )
			inFrame = false;

		// mark it
		if ( inFrame )
			sn->m_gbFrameNum = gbFrameNum;

		// custom xml tag, hash the tag itself
		if ( tid != TAG_XMLTAG ) continue;
		// stop at first space to avoid fields!!
		char *p    =     m_wptrs[ws] + 1;
		char *pend = p + m_wlens[ws];
		// skip back tags
		if ( *p == '/' ) continue;
		// reset hash
		int64_t xh = 0;
		// and hash char count
		unsigned char cnt = 0;
		// hash till space or / or >
		for ( ; p < pend ; p++ ) {
			// stop on space or / or >
			if ( is_wspace_a(*p) ) break;
			if ( *p == '/' ) break;
			if ( *p == '>' ) break;
			// hash it in
			xh ^= g_hashtab[cnt++][(unsigned char )*p];
		}
		// if it is a string of the same chars it can be 0
		if ( ! xh ) xh = 1;
		// store that
		sn->m_xmlNameHash = (int32_t)xh;
	}

	// find any open ended tags and constrain them based on their parent
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// get its parent
		Section *ps = si->m_parent;
		// if parent is open-ended panic!
		if ( ps && ps->m_b < 0 ) { char *xx=NULL;*xx=0; }

		// if our parent got constrained from under us, we need
		// to telescope to a new parent
		for  ( ; ps && ps->m_b >= 0 && ps->m_b <= si->m_a ; ) {
			ps = ps->m_parent;
			si->m_parent = ps;
		}

		// assume end is end of doc
		int32_t end = m_words->getNumWords();
		// get end of parent
		if ( ps ) end = ps->m_b;
		// flag it
		if ( si->m_b == -1 ) si->m_flags |= SEC_OPEN_ENDED;
		// shrink our section if parent ends before us OR if we
		// are open ended
		if ( si->m_b != -1 && si->m_b <= end ) continue;
		// this might constrain someone's parent such that
		// that someone no longer can use that parent!!
		si->m_b = end;
		// . get our tag type
		// . use int32_t instead of nodeid_t so we can re-set this
		//   to the xml tag hash if we need to
		int32_t tid1 = m_tids[si->m_a];
		// use the tag hash if this is an xml tag
		if ( tid1 == TAG_XMLTAG ) {
			// we computed this above
			tid1 = si->m_xmlNameHash;
			// skip if zero!
			if ( ! tid1 ) continue;
		}
		// must be there to be open ended
		if ( ! tid1 ) { char *xx=NULL;*xx=0; }
		// flag it for later
		//si->m_flags |= SEC_CONSTRAINED;
		// NOW, see if within that parent there is actually another
		// tag after us of our same tag type, then use that to
		// constrain us instead!!
		// this hurts <p><table><tr><td><p>.... because it
		// uses that 2nd <p> tag to constrain si->m_b of the first
		// <p> tag which is not right! sunsetpromotions.com has that.
		for ( int32_t j = i + 1 ; j < m_numSections ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Section *sj = &m_sections[j];
			// get word start
			int32_t a = sj->m_a;
			// skip if ties with us already
			if ( a == si->m_a ) continue;
			// stop if out
			if ( a >= end ) break;

			// . it must be in the same expanded frame src, if any
			// . this fixes trulia.com which was ending our html
			//   tag, which was open-ended, with the html tag in
			//   a frame src expansion
			if ( sj->m_gbFrameNum != si->m_gbFrameNum ) continue;
			// fix sunsetpromotions.com bug. see above.
			if ( sj->m_parent != si->m_parent ) continue;
			// get its tid
			int32_t tid2 = tids[a];
			// use base hash if xml tag
			if ( tid2 == TAG_XMLTAG )
				tid2 = sj->m_xmlNameHash;
			// must be our tag type!
			if ( tid2 != tid1 ) continue;
			// ok end us there instead!
			si->m_b = a;
			// stop
			break;
		}
	}


	// reparent again now that things are closed
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *si = &m_sections[i];
		// skip if we are still open-ended
		if ( si->m_b < 0 ) { char *xx=NULL;*xx=0; }
		// get parent
		Section *sp = si->m_parent;
		// skip if null
		if ( ! sp ) continue;
		// skip if parent still open ended
		if ( sp->m_b < 0 ) { char *xx=NULL;*xx=0; }
		// subloop it
	doagain2:
		// skip if no parent
		if ( ! sp ) continue;
		// . does parent contain our first word?
		// . it need not fully contain our last word!!!
		if ( sp->m_a <= si->m_a && sp->m_b > si->m_a ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_a <= si->m_a && sp->m_b == -1 ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_b == -1 ) { char *xx=NULL;*xx=0; }
		// get grandparent
		sp = sp->m_parent;
		// set
		si->m_parent = sp;
		// try again
		goto doagain2;
	}


	m_isTestColl = ! strcmp(m_coll,"qatest123") ;

	//
	//
	// now assign m_sectionPtrs[] which map a word to the first
	// section that contains it
	//
	//
	Section *dstack[MAXTAGSTACK];
	int32_t     ns = 0;
	int32_t      j = 0;
	current       = m_rootSection;//&m_sections[0];
	Section *next = m_rootSection;//&m_sections[0];
	// first print the html lines out
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// pop all off the stack that match us
		for ( ; ns>0 && dstack[ns-1]->m_b == i ; ) {
			ns--;
			current = dstack[ns-1];
		}
		// push our current section onto the stack if i equals
		// its first word #
		for ( ; next && i == next->m_a ; ) {
			dstack[ns++] = next;
			// sanity check
			//if ( next->m_a == next->m_b ) { char *xx=NULL;*xx=0;}
			// set our current section to this now
			current = next;
			// get next section for setting "next"
			j++;
			// if no more left, set "next" to NULL and stop loop
			if ( j >= m_numSections ) { next=NULL; break; }
			// grab it
			next = &m_sections[j];
		}
		// assign
		m_sectionPtrs[i] = current;
	}

	if ( m_isTestColl ) {
		// map each word to a section that contains it at least
		for ( int32_t i = 0 ; i < m_nw ; i++ ) {
			Section *si = m_sectionPtrs[i];
			if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
			if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
		}
	}

	// . addImpliedSections() requires Section::m_baseHash
	// . set Section::m_baseHash
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// these have to be in order of sn->m_a to work right
		// because we rely on the parent tag hash, which would not
		// necessarily be set if we were not sorted, because the 
		// parent section could have SEC_FAKE flag set because it is
		// a br section added afterwards.
		Section *sn = &m_sections[i];
		// get word start into "ws"
		int32_t ws = sn->m_a;
		// shortcut
		nodeid_t tid = tids[ws];
		// sanity check, <a> guys are not sections
		//if ( tid == TAG_A &&
		//     !(sn->m_flags & SEC_SENTENCE) ) { char *xx=NULL;*xx=0; }
		// use a modified tid as the tag hash?
		int64_t mtid = tid;
		// custom xml tag, hash the tag itself
		if ( tid == TAG_XMLTAG )
			mtid = hash32 ( wptrs[ws], wlens[ws] );
		// an unknown tag like <!! ...->
		if ( tid == 0 )
			mtid = 1;
		// . if we are a div tag, mod it
		// . treat the fields in the div tag as 
		//   part of the tag hash. 
		// . helps Events.cpp be more precise about
		//   section identification!!!!
		// . we now do this for TD and TR so Nov 2009 can telescope for
		//   http://10.5.1.203:8000/test/doc.17096238520293298312.html
		//   so the calendar title "Nov 2009" can affect all dates
		//   below the calendar.
		if ( tid == TAG_DIV  || 
		     tid == TAG_TD   || 
		     tid == TAG_TR   ||
		     tid == TAG_LI   || // newmexico.org urls class=xxx
		     tid == TAG_UL   || // newmexico.org urls class=xxx
		     tid == TAG_P    || // <p class="pstrg"> stjohnscollege.edu
		     tid == TAG_SPAN ) {
			// get ptr
		        uint8_t *p = (uint8_t *)wptrs[ws];
			// skip <
			p++;
			// skip following alnums, that is the tag name
			for ( ; is_alnum_a(*p) ; p++ );
			//if ( tid == TAG_DIV ) p += 4;
			// skip "<td" or "<tr"
			//else p += 3;
			// scan for "id" or "class" in it
			// . i had to increase this because we were missing
			//   some stuff causing us to get the wrong implied
			//   sections for 
			//   www.guysndollsllc.com/page5/page4/page4.html
			//   causing "The Remains" to be paired up with
			//   "Aug 7, 2010" in an implied section which was
			//   just wrong. it was 20, i made it 100...
			uint8_t *pend = p + 100;
			// position ptr
			unsigned char cnt = 0;
			// a flag
			bool skipTillSpace = false;
			// . just hash every freakin char i guess
			// . TODO: maybe don't hash "width" for <td><tr>
			for ( ; *p && *p !='>' && p < pend ; p++ ) {
				// skip bgcolor= tags because panjea.org
				// interlaces different colored <tr>s in the
				// table and i want them to be seen as brother
				// sections, mostly for the benefit of the
				// setting of lastBrother1/2 in Events.cpp
				if ( is_wspace_a(p[0])      &&
				     to_lower_a (p[1])=='b' &&
				     to_lower_a (p[2])=='g' ) {
					skipTillSpace = true;
					continue;
				}
				// and skip height=* tags so cabq.gov which
				// uses varying <td> height attributes will
				// have its google map links have the same
				// tag hash so TSF_PAGE_REPEAT gets set
				// in Events.cpp and they are not event titles.
				// it has other chaotic nested tag issues
				// so let's take this out.
				/*
				if ( is_wspace_a(p[0])      &&
				     to_lower_a (p[1])=='h' &&
				     to_lower_a (p[2])=='e' &&
				     to_lower_a (p[3])=='i' &&
				     to_lower_a (p[4])=='g' &&
				     to_lower_a (p[5])=='h' &&
				     to_lower_a (p[6])=='t' ) {
					skipTillSpace = true;
					continue;
				}
				*/

				// if not a space continue
				if ( skipTillSpace ) {
					if ( ! is_wspace_a(*p) ) continue;
					skipTillSpace = false;
				}
				// do not hash until we get a space
				if ( skipTillSpace ) continue;
				// skip if not alnum
				if ( !is_alnum_a(*p)) continue;
				// hash it in
				mtid ^= g_hashtab[cnt++][(unsigned char)*p];
			}
		}
		// should not have either of these yet!
		if ( sn->m_flags & SEC_FAKE     ) { char *xx=NULL;*xx=0; }
		if ( sn->m_flags & SEC_SENTENCE ) { char *xx=NULL;*xx=0; }
		// sanity check
		if ( mtid == 0 ) { char *xx=NULL;*xx=0; }
		// . set the base hash, usually just tid
		// . usually base hash is zero but if it is a br tag
		//   we set it to something special to indicate the number
		//   of br tags in the sequence
		sn->m_baseHash ^= mtid;
		// fix this
		if ( sn == rootSection ) sn->m_baseHash = 1;
		// fix root section i guess
		if ( sn->m_baseHash == 0 ) { 
			// fix core on gk21
			sn->m_baseHash = 2;
			//char *xx=NULL;*xx=0; }
		}
		// set this now too WHY? should already be set!!! was
		// causing the root section to become a title section
		// because first word was "<title>". then every word in
		// the doc got SEC_IN_TITLE set and did not get hashed
		// in XmlDoc::hashBody()... NOR in XmlDoc::hashTitle()!!!
		if ( sn != rootSection ) // || tid != TAG_TITLE ) 
			sn->m_tagId = tid;


		//
		// set m_turkBaseHash
		//
		// . using just the tid based turkTagHash is not as good
		//   as incorporating the class of tags because we can then
		//   distinguish between more types of tags in a document and
		//   that is kind of what "class" is used for
		//
		// use this = "Class" tid
		int64_t ctid = tid;
		// get ptr
		uint8_t *p = (uint8_t *)wptrs[ws];
		// skip <
		p++;
		// skip following alnums, that is the tag name
		for ( ; is_alnum_a(*p) ; p++ );
		// scan for "class" in it
		uint8_t *pend = p + 100;
		// position ptr
		unsigned char cnt = 0;
		// a flag
		bool inClass = false;
		// . just hash every freakin char i guess
		// . TODO: maybe don't hash "width" for <td><tr>
		for ( ; *p && *p !='>' && p < pend ; p++ ) {
			// hashing it up?
			if ( inClass ) {
				// all done if space
				if ( is_wspace_a(*p) ) break;
				// skip if not alnum
				//if ( !is_alnum_a(*p)) continue;
				// hash it in
				ctid ^= g_hashtab[cnt++][(unsigned char)*p];
			}
			// look for " class="
			if ( p[0] !=' ' ) continue;
			if ( to_lower_a(p[1]) != 'c' ) continue;
			if ( to_lower_a(p[2]) != 'l' ) continue;
			if ( to_lower_a(p[3]) != 'a' ) continue;
			if ( to_lower_a(p[4]) != 's' ) continue;
			if ( to_lower_a(p[5]) != 's' ) continue;
			if ( to_lower_a(p[6]) != '=' ) continue;
			// mark it
			inClass = true;
			// skip over it
			p += 6;
		}
		// if root section has no tag this is zero and will core
		// in Dates.cpp where it checks m_turkTagHash32 to be zero
		if ( ctid == 0 ) ctid = 999999;
		// set it for setting m_turkTagHash32
		sn->m_turkBaseHash = ctid;
		// always make root turkbasehash be 999999.
		// if root section did not start with tag it's turkBaseHash
		// will be 999999. but a root section that did start with
		// a tag will have a different turk base hash.
		// will be the same, even if one leads with some punct.
		// fix fandango.com and its kid.
		if ( sn == m_rootSection )
			sn->m_turkBaseHash = 999999;
	}


	// set up our linked list, the functions below will insert sections
	// and modify this linked list
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set it
		if ( i + 1 < m_numSections )
			m_sections[i].m_next = &m_sections[i+1];
		if ( i - 1 >= 0 )
			m_sections[i].m_prev = &m_sections[i-1];
	}

	// i would say <hr> is kinda like an <h0>, so do it first
	//splitSections ( "<hr" , (int32_t)BH_HR );

	// init to -1 to indicate none
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// reset it
		si->m_firstWordPos = -1;
		si->m_lastWordPos  = -1;
		si->m_senta        = -1;
		si->m_sentb        = -1;
		si->m_headRowSection = NULL;
		si->m_headColSection = NULL;
	}
	// now set position of first word each section contains
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not alnum word
		if ( ! m_wids[i] ) continue;
		// get smallest section containing
		Section *si = m_sectionPtrs[i];
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one!
			if ( si->m_firstWordPos >= 0 ) break;
			// otherwise, we are it
			si->m_firstWordPos = i;
			// . set format hash of it
			// . do it manually since tagHash not set yet
		}
	}
	// and last word position
	for ( int32_t i = m_nw - 1 ; i > 0 ; i-- ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not alnum word
		if ( ! m_wids[i] ) continue;
		// get smallest section containing
		Section *si = m_sectionPtrs[i];
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one!
			if ( si->m_lastWordPos >= 0 ) break;
			// otherwise, we are it
			si->m_lastWordPos = i;
		}
	}

	sec_t inFlag = 0;
	int32_t  istack[1000];
	sec_t iflags[1000];
	int32_t  ni = 0;
	// 
	// now set the inFlags here because the tags might not have all
	// been closed, making tags like SEC_STYLE overflow from where
	// they should be...
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// did we exceed a tag boundary?
		for ( ; ni>0 && si->m_a >= istack[ni-1] ; ) {
			// undo flag
			inFlag &= ~iflags[ni-1];
			// pop off
			ni--;
		}
		// get the flag if any into mf
		sec_t mf = 0;
		// skip if not special tag id
		nodeid_t tid = si->m_tagId;
		if      ( tid == TAG_SCRIPT  ) mf = SEC_SCRIPT;
		else if ( tid == TAG_TABLE   ) mf = SEC_IN_TABLE;
		else if ( tid == TAG_NOSCRIPT) mf = SEC_NOSCRIPT;
		else if ( tid == TAG_STYLE   ) mf = SEC_STYLE;
		else if ( tid == TAG_MARQUEE ) mf = SEC_MARQUEE;
		else if ( tid == TAG_SELECT  ) mf = SEC_SELECT;
		else if ( tid == TAG_STRIKE  ) mf = SEC_STRIKE;
		else if ( tid == TAG_S       ) mf = SEC_STRIKE2;
		else if ( tid == TAG_H1      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H2      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H3      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H4      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_TITLE   ) mf = SEC_IN_TITLE;
		// accumulate
		inFlag |= mf;
		// add in the flags
		si->m_flags |= inFlag;
		// skip if nothing special
		if ( ! mf ) continue;
		// sanity
		if ( ni >= 1000 ) { char *xx=NULL;*xx=0; }
		// otherwise, store on stack
		istack[ni] = si->m_b;
		iflags[ni] = mf;
		ni++;

		// title is special
		if ( tid == TAG_TITLE && m_titleStart == -1 ) {
			m_titleStart = si->m_a; // i;
			// Address.cpp uses this
			m_titleStartAlnumPos = alnumCount;
		}
	}

	// . now we insert sentence sections
	// . find the smallest section containing the first and last
	//   word of each sentence and inserts a subsection into that
	// . we have to be careful to reparent, etc.
	// . kinda copy splitSections() function
	// . maybe add an "insertSection()" function???
	if ( m_contentType != CT_JS ) {
		// add sentence sections
		if ( ! addSentenceSections() ) return true;
		// this is needed by setSentFlags()
		setNextSentPtrs();
		// returns false and sets g_errno on error
		if ( ! setSentFlagsPart1 ( ) ) return true;
	}

	// . set m_nextBrother
	// . we call this now to aid in setHeadingBit() and for adding the
	//   implied sections, but it is ultimately
	//   called a second time once all the new sections are inserted
	setNextBrotherPtrs ( false ); // setContainer = false

	// . set SEC_HEADING bit
	// . need this before implied sections
	setHeadingBit ();

	//
	// set SEC_HR_CONTAINER bit for use by addHeaderImpliedSections(true)
	// fix for folkmads.org which has <tr><td><div><hr></div>...
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not hr
		if ( si->m_tagId != TAG_HR ) continue;
		// cycle this
		Section *sp = si;
		// blow up right before it has text
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// set this
			sp->m_flags |= SEC_HR_CONTAINER;
			// stop if parent has text
			if ( sp->m_parent &&
			     sp->m_parent->m_firstWordPos >= 0 ) break;
		}
	}

	setTagHashes();

	//
	//
	// TODO TODO
	//
	// TAKE OUT THESE SANITY CHECKS TO SPEED UP!!!!!!
	//
	//

	// we seem to be pretty good, so remove these now. comment this
	// goto out if you change sections.cpp and want to make sure its good
	//if ( m_isTestColl ) verifySections();

	// clear this
	bool isHidden  = false;
	int32_t startHide = 0x7fffffff;
	int32_t endHide   = 0 ;
	//int32_t numTitles = 0;
	//Section *lastTitleParent = NULL;
	Section *firstTitle = NULL;
	// now that we have closed any open tag, set the SEC_HIDDEN bit
	// for all sections that are like <div style=display:none>
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set m_lastSection so we can scan backwards
		m_lastSection = sn;
		// set SEC_SECOND_TITLE flag
		if ( sn->m_tagId == TAG_TITLE ) {
			// . reset if different parent.
			// . fixes trumba.com rss feed with <title> tags
			if ( firstTitle &&
			     firstTitle->m_parent != sn->m_parent ) 
				firstTitle = NULL;
			// inc the count
			//numTitles++;
			// 2+ is bad. fixes wholefoodsmarket.com 2nd title
			if ( firstTitle ) sn->m_flags |= SEC_SECOND_TITLE;
			// set it if need to
			if ( ! firstTitle ) firstTitle = sn;
			// store our parent
			//lastTitleParent = sn->m_parent;
		}
		// set this
		int32_t wn = sn->m_a;
		// stop hiding it?
		if ( isHidden ) {
			// turn it off if not contained
			if      ( wn >= endHide   ) isHidden = false;
			//else if ( wn <= startHide ) isHidden = false;
			else    sn->m_flags |= SEC_HIDDEN;
		}
		// get tag id
		nodeid_t tid = sn->m_tagId;//tids[wn];
		// is div, td or tr tag start?
		if ( tid!=TAG_DIV && 
		     tid!=TAG_TD && 
		     tid!=TAG_TR &&
		     tid!=TAG_UL &&
		     tid!=TAG_SPAN) continue;

		// . if we are a div tag, mod it
		// . treat the fields in the div tag as 
		//   part of the tag hash. 
		// . helps Events.cpp be more precise about
		//   section identification!!!!
		// . we now do this for TD and TR so Nov 2009 can telescope for
		//   http://10.5.1.203:8000/test/doc.17096238520293298312.html
		//   so the calendar title "Nov 2009" can affect all dates
		//   below the calendar.

		// get the style tag in there and check it for "display: none"!
		int32_t  slen = wlens[wn];
		char *s    = wptrs[wn];
		char *send = s + slen;
		// check out any div tag that has a style
		char *style = gb_strncasestr(s,slen,"style=") ;
		if ( ! style ) continue;
		// . check for hidden
		// . if no hidden tag assume it is UNhidden
		// . TODO: later push & pop on stack
		char *ds = gb_strncasestr(style,send-style,"display:");
		// if display:none not found turn off SEC_HIDDEN
		if ( ! ds || ! gb_strncasestr(s,slen,"none") ) {
			// turn off the hiding
			isHidden = false;
			// off in us too
			sn->m_flags &= ~SEC_HIDDEN;
			continue;
		}
		// mark all sections in this with the tag
		isHidden = true;
		// on in us
		sn->m_flags |= SEC_HIDDEN;
		// stop it after this word for sure
		if ( sn->m_b   > endHide   ) endHide   = sn->m_b;
		if ( sn->m_a < startHide ) startHide = sn->m_a;
	}


	// now set the content hash of each section
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be an alnum word
		if ( ! m_wids[i] ) continue;
		// get its section
		m_sectionPtrs[i]->m_contentHash64 ^= m_wids[i];
		// fix "smooth smooth!"
		if ( m_sectionPtrs[i]->m_contentHash64 == 0 )
			m_sectionPtrs[i]->m_contentHash64 = 123456;

	}

	// reset
	m_numInvalids = 0;

	// now set SEC_NOTEXT flag if content hash is zero!
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( sn->m_contentHash64 ) continue;
		// no text!
		sn->m_flags |= SEC_NOTEXT;
		// count it
		m_numInvalids++;
	}

	//
	// set m_sentenceContentHash for sentence that need it
	//
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;

		// no, m_contentHash64 is just words contained 
		// directly in the section... so since a sentence can
		// contain like a bold subsection, we gotta scan the
		// whole thing.
		sn->m_sentenceContentHash64 = 0LL;

		// scan the wids of the whole sentence, which may not
		// be completely contained in the "sn" section!!
		int32_t a = sn->m_senta;
		int32_t b = sn->m_sentb;
		for ( int32_t j = a ; j < b ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// must be an alnum word
			if ( ! m_wids[j] ) continue;
			// get its section
			sn->m_sentenceContentHash64 ^= m_wids[j];
			// fix "smooth smooth!"
			if ( sn->m_sentenceContentHash64 == 0 )
				sn->m_sentenceContentHash64 = 123456;
		}
	}


	////////
	//
	// set Section::m_indirectSentHash64
	//
	////////
	for ( Section *sn = m_firstSent ; sn ; sn = sn->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		int64_t sc64 = sn->m_sentenceContentHash64;
		if ( ! sc64 ) { char *xx=NULL;*xx=0; }
		// propagate it upwards
		Section *p = sn;
		// TODO: because we use XOR for speed we might end up with
		// a 0 if two sentence are repeated, they cancel out..
		for ( ; p ; p = p->m_parent )
			p->m_indirectSentHash64 ^= sc64;
	}

	/////
	//
	// set SEC_HASHXPATH
	//
	/////
	for ( Section *sn = m_firstSent ; sn ; sn = sn->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		int64_t sc64 = sn->m_sentenceContentHash64;
		if ( ! sc64 ) { char *xx=NULL;*xx=0; }
		// propagate it upwards
		Section *p = sn->m_parent;
		// parent of sentence always gets it i guess
		uint64_t lastVal = 0x7fffffffffffffffLL;
		// TODO: because we use XOR for speed we might end up with
		// a 0 if two sentence are repeated, they cancel out..
		for ( ; p ; p = p->m_parent ) {
			// how can this be a text node?
			if ( p->m_tagId == TAG_TEXTNODE ) continue;
			// if parent's hash is same as its kid then do not
			// hash it separately in order to save index space
			// from adding gbxpathsitehash1234567 terms
			if ( p->m_indirectSentHash64 == lastVal ) continue;
			// update this for deduping
			lastVal = p->m_indirectSentHash64;
			// this parent should be hashed with gbxpathsitehash123
			p->m_flags |= SEC_HASHXPATH;
		}
	}

	//
	// set Section::m_alnumPosA/m_alnumPosB
	//
	int32_t alnumCount2 = 0;
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// save this
		sn->m_alnumPosA = alnumCount2;
		// scan the wids of the whole sentence, which may not
		// be completely contained in the "sn" section!!
		int32_t a = sn->m_senta;
		int32_t b = sn->m_sentb;
		for ( int32_t j = a ; j < b ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// must be an alnum word
			if ( ! m_wids[j] ) continue;
			// alnumcount
			alnumCount2++;
		}
		// so we contain the range [a,b), typical half-open interval
		sn->m_alnumPosB = alnumCount2;
		// sanity check
		if ( sn->m_alnumPosA == sn->m_alnumPosB ){char *xx=NULL;*xx=0;}

		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if already had one!
			if ( si->m_alnumPosA > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosA = sn->m_alnumPosA;
		}

	}
	// propagate up alnumPosB now
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if already had one! no, because we need to
			// get the MAX of all of our kids!!
			//if ( si->m_alnumPosB > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosB = sn->m_alnumPosB;
		}
	}
	m_alnumPosValid = true;


	/////////////////////////////
	//
	// set Section::m_rowNum and m_colNum for sections in table
	//
	// (we use this in Dates.cpp to set Date::m_tableRow/ColHeaderSec
	//  for detecting certain col/row headers like "buy tickets" that
	//  we use to invalidate dates as event dates)
	//
	/////////////////////////////
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// need table tag
		if ( si->m_tagId != TAG_TABLE ) continue;
		// set all the headCol/RowSection ptrs rownum, colnum, etc.
		// just for this table.
		setTableRowsAndCols ( si );
	}

	// now NULLify either all headRow or all headColSections for this
	// table. dmjuice.com actually uses headRows and not columns. so
	// we need to detect if the table header is the first row or the first
	// column.
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// need table tag
		if ( si->m_tagId != TAG_TABLE ) continue;
		// ok, now set table header bits for that table
		setTableHeaderBits ( si );
	}

	// propagate m_headCol/RowSection ptr to all kids in the td cell

	// . "ot" = occurence table
	// . we use this to set Section::m_occNum and m_numOccurences
	if ( ! m_ot.set (4,8,5000,NULL, 0 , false ,m_niceness,"sect-occrnc") )
		return true;

	// set the m_ot hash table for use below
	for ( int32_t i = 1 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// assume no vote
		uint64_t vote = 0;
		// try to get it
		uint64_t *vp = (uint64_t *)m_ot.getValue ( &sn->m_tagHash );
		// assume these are zeroes
		int32_t occNum = 0;
		// if there, set these to something
		if ( vp ) {
			// set it
			vote = *vp;
			// what section # are we for this tag hash?
			occNum = vote & 0xffffffff;
			// save our kid #
			sn->m_occNum = occNum;
			// get ptr to last section to have this tagHash
			//sn->m_prevSibling = (Section *)(vote>>32);
		}
		// mask our prevSibling
		vote &= 0x00000000ffffffff;
		// we are the new prevSiblinkg now
		vote |= ((uint64_t)((uint32_t)i))<<32; // rplcd sn w/ i
		// inc occNum for the next guy
		vote++;
		// store back. return true with g_errno set on error
		if ( ! m_ot.addKey ( &sn->m_tagHash , &vote ) ) return true;

		// use the secondary content hash which will be non-zero
		// if the section indirectly contains some text, i.e.
		// contains a subsection which directly contains text
		//int32_t ch = sn->m_contentHash;
		// skip if no content 
		//if ( ! ch ) continue;
		// use this as the "modified taghash" now
		//int32_t modified = sn->m_tagHash ^ ch;
		// add the content hash to this table as well!
		//if ( ! m_cht2.addKey ( &modified ) ) return true;
	}

	// . now we define SEC_UNIQUE using the tagHash and "ot"
	// . i think this is better than using m_tagHash
	// . basically you are a unique section if you have no siblings
	//   in your container 

	// . set Section::m_numOccurences
	// . the first section is the god section and has a 0 for tagHash
	//   so skip that!
	for ( int32_t i = 1 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// get it
		uint64_t vote ;
		// assign it
		uint64_t *slot = (uint64_t *)m_ot.getValue ( &sn->m_tagHash );
		// wtf? i've seen this happen once in a blue moon
		if ( ! slot ) {
			log("build: m_ot slot is NULL! wtf?");
			continue;
		}
		// otherwise, use it
		vote = *slot;
		// get slot for it
		int32_t numKids = vote & 0xffffffff;
		// must be at least 1 i guess
		if ( numKids < 1 && i > 0 ) { char *xx=NULL;*xx=0; }
		// how many siblings do we have total?
		sn->m_numOccurences = numKids;
		// sanity check
		if ( sn->m_a < 0    ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b > m_nw ) { char *xx=NULL;*xx=0; }
		//
		// !!!!!!!!!!!!!!!! HEART OF SECTIONS !!!!!!!!!!!!!!!!
		//
		// to be a valid section we must be the sole section 
		// containing at least one alnum word AND it must NOT be
		// in a script/style/select/marquee tag
		//if ( sn->m_exclusive>0 && !(sn->m_flags & badFlags) )
		//	continue;
		// this means we are useless
		//sn->m_flags |= SEC_NOTEXT;
		// and count them
		//m_numInvalids++;
	}

	///////////////////////////////////////
	//
	// now set Section::m_listContainer
	//
	// . a containing section is a section containing
	//   MULTIPLE smaller sections
	// . so if a section has a containing section set its m_listContainer
	//   to that containing section
	// . we limit this to sections that directly contain text for now
	// . Events.cpp::getRegistrationTable() uses m_nextBrother so we
	//   need this now!!
	//
	///////////////////////////////////////
	setNextBrotherPtrs ( true ); // setContainer = true


	///////////////////////////////////////
	//
	// now set SEC_MENU and SEC_LINK_TEXT flags
	//
	///////////////////////////////////////
	setMenus();

	//verifySections();

	// don't use nsvt/osvt now
	return true;
}

// place name indicators
static HashTableX s_pit;
static char s_pitbuf[2000];
static bool s_init9 = false;

bool initPlaceIndicatorTable ( ) {
	if ( s_init9 ) return true;
	// init place name indicators
	static char *s_pindicators [] = {
		"airport",
		"airstrip",
		"auditorium",
		//"area",
		"arena",
		"arroyo",
		// wonderland ballroom thewonderlandballroom.com
		"ballroom",
		"bank",
		"banks",
		"bar",
		"base",
		"basin",
		"bay",
		"beach",
		"bluff",
		"bog",
		//"boundary",
		"branch",
		"bridge",
		"brook",
		"building",
		"bunker",
		"burro",
		"butte",
		"bookstore", // st john's college bookstore
		"enterprises", // J & M Enterprises constantcontact.com
		"llc",  // why did we remove these before???
		"inc",
		"incorporated",
		"cabin",
		"camp",
		"campground",
		"campgrounds",
		// broke "Tennis on Campus" event
		//"campus",
		"canal",
		"canyon",
		"casa",
		"castle",
		"cathedral",
		"cave",
		"cemetery",
		"cellar", // the wine cellar
		"center",
		"centre",
		// channel 13 news
		//"channel",
		"chapel",
		"church",
		// bible study circle
		//"circle",
		"cliffs",
		"clinic",
		"college",
		"company",
		"complex",
		"corner",
		"cottage",
		"course",
		"courthouse",
		"courtyard",
		"cove",
		"creek",
		"dam",
		"den",
		// subplace
		//"department",
		"depot",
		"dome",
		"downs",
		//"fair", // xxx fair is more of an event name!!
		"fairgrounds",
		"fairground",
		"forum",
		"jcc",

		"playground",
		"playgrounds",
		"falls",
		"farm",
		"farms",
		"field",
		"fields",
		"flat",
		"flats",
		"forest",
		"fort",
		//"fountain",
		"garden",
		"gardens",
		//"gate",
		"glacier",
		"graveyard",
		"gulch",
		"gully",
		"hacienda",
		"hall",
		//"halls",
		"harbor",
		"harbour",
		"hatchery",
		"headquarters",
		//"heights",
		"heliport",
		// nob hill
		//"hill",
		"hillside",
		"hilton",
		"historical",
		"historic",
		"holy",
		// members home
		//"home",
		"homestead",
		"horn",
		"hospital",
		"hotel",
		"house",
		"howard",
		"inlet",
		"inn",
		"institute",
		"international",
		"isla",
		"island",
		"isle",
		"islet",
		"junction",
		"knoll",
		"lagoon",
		"laguna",
		"lake",
		"landing",
		"ledge",
		"lighthouse",
		"lodge",
		"lookout",
		"mall",
		"manor",
		"marina",
		"meadow",
		"mine",
		"mines",
		"monument",
		"motel",
		"museum",
		// subplace
		//"office",
		"outlet",
		"palace",
		"park",
		"peaks",
		"peninsula",
		"pit",
		"plains",
		"plant",
		"plantation",
		"plateau",
		"playa",
		"plaza",
		"point",
		"pointe",
		"pond",
		"pool", // swimming pool
		"port",
		"railroad",
		"ramada",
		"ranch",
		// rio rancho
		//"rancho",
		// date range
		//"range",
		"reef",
		"refure",
		"preserve", // nature preserve
		"reserve",
		"reservoir",
		"residence",
		"resort",
		//"rio",
		//"river",
		//"riverside",
		//"riverview",
		//"rock",
		"sands",
		"sawmill",
		"school",
		"schoolhouse",
		"shore",
		"spa",
		"speedway",
		"spring",
		"springs",
		"stadium",
		"station",
		"strip",
		"suite",
		"suites",
		"temple",
		"terrace",
		"tower",
		//"trail",
		"travelodge",
		"triangle",
		"tunnel",
		"university",
		//"valley",
		"wall",
		"ward",
		"waterhole",
		"waters",
		"well",
		"wells",
		"wilderness",
		"windmill",
		"woodland",
		"woods",
		"gallery",
		"theater",
		"theatre",
		"cinema",
		"cinemas",
		"playhouse",
		"saloon",
		"nightclub", // guys n' dolls restaurant and night club
		"lounge",
		"ultralounge",
		"brewery",
		"chophouse",
		"tavern",
		"company",
		"rotisserie",
		"bistro",
		"parlor",
		"studio",
		"studios",
		// denver, co
		//"co",
		"bureau",
		//"estates",
		"dockyard",
		"gym",
		"synagogue",
		"shrine",
		"mosque",
		"store",
		"mercantile",
		"mart",
		"amphitheatre",
		"kitchen",
		"casino",
		"diner",
		"eatery",
		"shop",
		//"inc",
		//"incorporated",
		//"corporation",
		//"limited",
		//"llc",
		//"foundation",
		"warehouse",
		"roadhouse",
		"foods",
		"cantina",
		"steakhouse",
		"smokehouse",
		"deli",
		//"enterprises",
		//"repair",
		//"service",
		// group services
		//"services",
		//"systems",
		"salon",
		"boutique",
		"preschool",
		//"galleries",
		"bakery",
		"factory",
		//"llp",
		//"attorney",
		//"association",
		//"solutions",
		"facility",
		"cannery",
		"winery",
		"mill",
		"quarry",
		"monastery",
		"observatory",
		"nursery",
		"pagoda",
		"pier",
		"prison",
		//"post",
		"ruin",
		"ruins",
		"storehouse",
		"square",
		"tomb",
		"wharf",
		"zoo",
		"mesa",
		// five day pass
		//"pass",
		"passage",
		"peak",
		"vineyard",
		"vineyards",
		"grove",
		"space",
		"library",
		"bakery", // a b c bakery
		"school",
		"church",
		"park",
		"house",
		//"market",  hurt Los Ranchos Growers' Market
		//"marketplace",
		"university",
		"center",
		"restaurant",
		"bar",
		"grill",
		"grille",
		"cafe",
		"cabana",
		"shack",
		"shoppe",
		"collesium",
		"colliseum",
		"pavilion"
		//"club"
	};
	// store these words into table
	s_init9 = true;
	s_pit.set(8,0,128,s_pitbuf,2000,false,0,"ti1tab");
	int32_t n = (int32_t)sizeof(s_pindicators)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// set words
		char *s = s_pindicators[i];
		int64_t h = hash64n(s);
		s_pit.addKey ( &h );
	}
	return true;
}

static HashTableX s_igt;
static char s_igtbuf[10000];
static bool s_init3 = false;

void initGenericTable ( int32_t niceness ) {

	// . now we create strings of things that are somewhat generic
	//   and should be excluded from titles. 
	// . we set the GENERIC bit for each word or phrase in the sentence
	//   that matches one in this list of generic words/phrases
	// . then we ignore the longest generic phrase that is two words or
	//   more, within the sentence, for the purposes of forming titles
	// . so if we had "buy tickets for Spiderman" we would ignore
	//   "buy tickets for" and the title would just be Spiderman
	// . we also set the GENERIC bit for dates and places that match
	//   that of the event in addition to these phrases/words
	static char *s_ignore[] = {
		"repeats",
		"various", // this event repeats on various days
		"feed",
		"size", // small class size: hadcolon algo fix
		"readers",
		"rating",
		"publish",
		"category",
		"special",
		"guest",
		"guests",
		"sold", // sold out
		"out",
		"tba",
		"promotional", // promotional partners
		"partners",
		"regular", // regular ticket price
		"fee",
		"fees",
		"purchase",
		"how",
		"order", // how to order
		"redeem",
		"promo", // promo code
		"code",
		"genre",
		"type",
		"price",
		"prices", // ticket prices
		"priced",
		"pricing", // pricing information
		"bid", // minimum bid: $50,000
		"bids",
		"attn",
		"pm",
		"am",
		"store", // fix store: open blah blah... for hadcolon
		"default", // fix default: all about... title for had colon
		"id", // event id: 1234
		"buffer", // fix losing Life of Brian to Buffer:33% title
		"coming",
		"soon",
		"deadline", // price deadline
		"place", // place order
		"order", 
		"users",
		"claim",
		"it",
		"set", // set: 10:30pm  (when they start the set i guess)
		"transportation",
		"organization",
		"company",
		"important",
		"faq",
		"faqs",
		"instructions",
		"instruction",
		"advertise",
		"advertisement",
		"name",
		"upcoming",
		"attraction",
		"attractions",
		"events",
		"news", // news & events
		"posted", // posted by, posted in news
		"is",
		"your",
		"user",
		"reviews",
		"most",
		"recent",
		"latest",
		"comments",
		"bookmark",
		"creator",
		"tags",
		"close",
		"closes",
		"closed",
		"send",
		"map",
		"maps",
		"directions",
		"driving",
		"help",
		"read", // read more
		"more",
		"every", // every other wednesday
		"availability",
		"schedule",
		"scheduling", // scheduling a program
		"program",
		"other",
		"log",
		"sign",
		"up",
		"login",
		"logged", // you're not logged in
		"you're",
		"should", // you should register
		"register",
		"registration", // registration fees
		"back",
		"change",
		"write",
		"save",
		"add",
		"share",
		"forgot",
		"password",
		"home",
		"hourly", // hourly fee
		"hours",
		"operation", // hours of operation
		"public", // hours open to the public.. public tours
		"what's",
		"happening",
		"menu",
		"plan", // plan a meeting
		"sitemap",
		"advanced",
		"beginner", // dance classes
		"intermediate",
		"basic",
		"beginners",
		// beginning hebrew conversation class?
		// 6 Mondays beginning Feb 7th - Mar 14th...
		"beginning", 

		"calendar", // calendar for jan 2012
		"full", // full calendar
		"hello", // <date> hello there guest
		"level", // dance classes
		"open", // open level
		"go",
		"homepage",
		"website",
		"view",
		"submit",
		"get",
		"subscribe",
		"loading",
		"last",
		"modified",
		"updated",

		"untitled", // untitled document for nonamejustfriends.com
		"document",
		"none", // none specified
		"specified",
		"age", // age suitability
		"suitability",
		"requirement", // age requirement
		"average", // average ratings
		"ratings",
		"business", // business categories
		"categories",
		"narrow", // narrow search
		"related", // related topics/search
		"topics",
		"searches",
		"sort", // sort by

		"there",
		"seating", // there will be no late seating
		"seats", // seats are limited
		"accomodations", // find accomodations
		"request", // request registration
		"requests",
		
		"are",
		"maximum", // maximum capacity
		"max", // maximum capacity
		"capacity", // capcity 150 guests
		"early",
		"late", // Wed - Thurs 1am to Late Morning
		"latest", // latest performance
		"performance", 
		"performances", // performances: fridays & saturdays at 8 PM..
		"rsvp", // rsvp for free tickets
		"larger", // view larger map

		"special",
		"guest",
		"guests",
		"venue",
		"additional",
		"general",
		"admission",
		"information",
		"info",
		"what",
		"who",
		"where",
		"when",
		"tickets",
		"ticket",
		"tix", // buy tix
		"tuition",
		"tuitions",
		"donate",
		"donation", // $4 donation - drivechicago.com
		"donations",
		"booking",
		"now",
		"vip",
		"student",
		"students",
		"senior",
		"seniors",
		"sr",
		"mil",
		"military",
		"adult",
		"adults",
		"teen",
		"teens",
		"tween",
		"tweens",
		"elementary",
		"preschool",
		"preschl",
		"toddler",
		"toddlers",
		"tdlr",
		"family",
		"families",
		"children",
		"youth",
		"youngsters",
		"kids",
		"kid",
		"child",

		"find", // find us
		"next",
		"prev",
		"previous",
		"series", // next series

		"round", // open year round

		// weather reports!
		"currently", // currently: cloudy (hadcolon fix)
		"humidity",
		"light",
		"rain",
		"rainy",
		"snow",
		"cloudy",
		"sunny",
		"fair",
		"windy",
		"mostly", // mostly cloudy
		"partly", // partly cloudy
		"fahrenheit",
		"celsius",
		// "Wind: N at 7 mph"
		"wind",
		"mph",
		"n",
		"s",
		"e",
		"w",
		"ne",
		"nw",
		"se",
		"sw",

		// lottery title: %16.0 million
		"million",

		// 6th street
		"1st",
		"2nd",
		"3rd",
		"4th",
		"5th",
		"6th",
		"7th",
		"8th",
		"9th",

		"thanksgiving",
		"year",
		"yearly",
		"year's",
		"day",
		"night",
		"nightly",
		"daily",
		"christmas",

		"fall",
		"autumn",
		"winter",
		"spring",
		"summer",
		"season", // 2011 sesason schedule of events

		"session", // fall session
		"sessions", // fall sessions

		"jan",
		"feb",
		"mar",
		"apr",
		"may",
		"jun",
		"jul",
		"aug",
		"sep",
		"oct",
		"nov",
		"dec",
		"january",
		"february",
		"march",
		"april",
		"may",
		"june",
		"july",
		"august",
		"september",
		"october",
		"novemeber",
		"december",
		"gmt",
		"utc",
		"cst",
		"cdt",
		"est",
		"edt",
		"pst",
		"pdt",
		"mst",
		"mdt",

		"morning",
		"evening",
		"afternoon",
		"mornings",
		"evenings",
		"afternoons",

		"monday",
		"tuesday",
		"wednesday",
		"thursday",
		"friday",
		"saturday",
		"sunday",

		"mondays",
		"tuesdays",
		"wednesdays",
		"thursdays",
		//"fridays", // hurts "5,000 Fridays" title for when.com
		"saturdays",
		"sundays", // all sundays

		"mwth",
		"mw",
		"mtw",
		"tuf",
		"thf",

		"tomorrow",
		"tomorrow's",
		"today",
		"today's",

		// we should probably treat these times like dusk/dawn
		"noon",
		"midday",
		"midnight",
		"sunset",
		"sundown",
		"dusk",
		"sunrise",
		"dawn",
		"sunup",

		"m",
		"mo",
		"mon",
		"tu",
		"tue",
		"tues",
		"wed",
		"weds",
		"wednes",
		"th",
		"thu",
		"thur",
		"thr",
		"thurs",
		"f",
		"fr",
		"fri",
		"sa",
		"sat",
		"su",
		"sun",

		"discount",
		"support",
		"featuring",
		"featured", // featured events
		"features", // more features
		"featuring",
		"presents",
		"presented",
		"presenting",
		"miscellaneous",

		"usa",
		"relevancy",
		"date",
		"time", // time:
		"showtime",
		"showtimes",
		"distance",
		"pacific", // pacific time
		"eastern",
		"central",
		"mountain",
		"cost", // cost:
		"per", // $50 per ticket
		"description",
		"buy",
		"become", // become a sponsor
		"twitter",
		"hashtag",
		"digg",
		"facebook",
		"like",
		"you",
		"fan", // become a facebook fan
		"media", // media sponsor
		"charity",
		"target", // Target:
		"now", // buy tickets now


		"reminder", // event remind
		"sponsors", // event sponsors
		"sponsor",
		"questions", // questions or comments
		"question",
		"comment", // question/comment
		"message",
		"wall", // "comment wall"
		"board", // "comment board" message board
		"other", // other events
		"ongoing", // other ongoing events
		"recurring",
		"repeating", // more repeating events
		"need", // need more information
		"quick", // quick links
		"links", // quick links
		"link",
		"calendar", // calendar of events
		"class", // class calendar
		"classes", // events & classes
		"schedule", // class schedule
		"activity", // activity calendar
		"typically",
		"usually",
		"normally",
		"some", // some saturdays
		"first",
		"second",
		"third",
		"fourth",
		"fifth",

		"city", // "city of albuquerque title

		"instructors", // 9/29 instructors:
		"instructor",
		"advisor", // hadcolon algo fix: Parent+Advisor:+Mrs.+Boey
		"advisors",
		"adviser",
		"advisers",
		"caller", // square dance field
		"callers", 
		"dj", // might be dj:
		"browse", // browse nearby
		"nearby",
		"restaurants", // nearby restaurants
		"restaurant",
		"bar",
		"bars",

		// why did i take these out??? leave them in the list, i
		// think these events are too generic. maybe because sometimes
		// that is the correct title and we shouldn't punish the
		// title score, but rather just filter the entire event 
		// because of that...?
		//"dinner", // fix "6:30 to 8PM: Dinner" title for la-bike.org
		//"lunch",
		//"breakfast",

		"served",
		"serving",
		"serves",
		"notes",
		"note",
		"announcement",
		"announcing",
		"first", // first to review
		"things", // things you must see
		"must",
		"see",
		"discover", // zvents: discover things to do
		"do",
		"touring", // touring region
		"region",
		"food",
		"counties",
		"tours",
		"tour",
		"tell", // tell a friend
		"friend", // tell a friend
		"about",
		"this",
		"occurs", // this event occurs weekly
		"weekly",

		"person",
		"group",
		"groups", // groups (15 or more)
		"our", // our story (about us)
		"story", // our story
		"special", // special offers
		"offers",
		//"bars", // nearby bars
		"people", // people who viewed this also viewed
		"also",
		"viewed",
		"additional", // additional service
		"real", // real reviews
		"united",
		"states",
		"over", // 21 and over only
		"advance", // $12 in adance / $4 at the door
		"list", // event list
		"mi",
		"miles",
		"km",
		"kilometers",
		"yes",
		"no",
		"false", // <visible>false</visible>
		"true",

		"usd", // currency units (eventbrite)
		"gbp", // currency units (eventbrite)

		"st", // sunday july 31 st
		"chat", // chat live
		"live",
		"have",
		"feedback",
		"dining", // dining and nightlife
		"nightlife", 
		"yet", // no comments yet
		"welcome", // stmargarets.com gets WELCOME as title
		"cancellation",
		"cancellations",
		"review",
		"preview",
		"previews",
		"overview",
		"overviews",
		"gallery", // gallery concerts
		"premium",
		"listing",
		"listings",
		"press",
		"releases", // press releases
		"release",  // press release
		"opening",
		"openings",
		"vip",
		"video",
		"audio",
		"radio",
		"yelp",
		"yahoo",
		"google",
		"mapquest",
		"quest", // map quest
		"microsoft",
		"eventbrite",
		"zvents",
		"zipscene",
		"eventful",
		"com", // .com
		"org", // .org
		"areas", // areas covered
		"covered",
		"cover", // cover charge
		"charge",
		"detail",
		"details",
		"phone",
		"tel",
		"telephone",
		"voice",
		"data",
		"ph",
		"tty",
		"tdd",
		"fax",
		"email",
		"e",
		"sale", // on sale now
		"sales", // Sales 7:30 pm Fridays
		"club", // club members
		"join",
		"please", // please join us at our monthly meetings
		"official", // official site link
		"site",
		"blog",
		"blogs", // blogs & sites
		"sites",
		"mail",
		"mailing", // mailing address
		"postal",
		"statewide", // preceeds phone # in unm.edu
		"toll",
		"tollfree",
		"call",
		"number",
		"parking",
		"limited", // limited parking available
		"available", // limited parking available
		"accepts", // accepts credit cards
		"accept",
		"visa", // we accept visa and mastercard
		"mastercard",
		//"jump", // jump to navigation
		"credit",
		"method",
		"methods", // methods of payment
		"payment",
		"payments",
		"cards",
		"zip", // POB 4321 zip 87197
		"admin", // 1201 third nw admin
		"meetings",
		"meeting",
		"meetup",
		"meets",
		"meet",
		"future", // other future dates & times (zvents)
		"dates",
		"times",
		"range", // price range (yelp.com blackbird buvette)
		"write" , // write a review
		"a",
		"performers", // performers at this event
		"band",
		"bands",
		"concert",
		"concerts",
		"hide" , // menu cruft from zvents
		"usa", // address junk
		"musicians", // category
		"musician", // category
		"current", // current weather
		"weather",
		"forecast", // 7-day forecast
		"contact",
		"us",
		"member",
		"members",
		"get", // get involved
		"involved",
		"press", // press room
		"room",
		"back", // back a page
		"page",
		"take", // take me to the top
		"me",
		"top", // top of page
		"print", // print friendly page
		"friendly",
		"description",
		"location",
		"locations",
		"street",
		"address",
		"neighborhood",
		"neighborhoods",
		"guide", // albuquerque guide for bird walk
		"ticketing",
		"software", // ticketing software
		"download",// software download
		"search",
		"results",
		"navigation", // main navigation
		"breadcrumb", // breadcrumb navigation
		"main",
		"skip", // skip to main content
		"content",
		"start",
		"starts", // starts 9/17/2011
		"starting", // starting in 2010
		"ends",
		"end",
		"ending",
		"begin",
		"begins" // concert begins at 8
		"beginning",
		"promptly", // starts promptly
		"will", // will begin
		"visit", // visit our website
		"visitors",
		"visitor",
		"visiting", // visiting hours
		"come", // come visit
		"check", // check us out
		"site",
		"website",
		"select", // select saturdays
		"begin",
		"ends", // ends 9/17/2011
		"multiple", // multiple dates
		"hottest", // hottest upcoming event
		"cancel",
		"displaying",
		"ordering", // ordering info from stjohnscollege
		"edit", // edit business info from yelp
		"of",
		"the",
		"and",
		"at",
		"to",
		"be",
		"or",
		"not",
		"in",
		"on",
		"only", // saturday and sunday only
		"winter", // winter hours: oct 15 - march 15 (unm.edu)
		"summer",  // summer hours
		"spring",
		"fall",

		"by",
		"under",
		"for",
		"from",
		"click",
		"here",
		"new",
		"free",
		"title",
		"event",
		"tbd", // title tbd
		"adv",
		"dos",
		"day",
		"days", // this event repeats on various days
		"week", // day(s) of the week
		"weekend",
		"weekends",
		"two", // two shows
		"runs", // show runs blah to blah (woodencowgallery.com)
		"show",
		"shows",
		"door",
		"doors",
		"gate", // gates open at 8
		"gates",
		"all",
		"ages",
		"admitted", // all ages admitted until 5pm (groundkontrol.com)
		"admittance",
		"until",
		"rights", // all rights reserved
		"reserved", // all rights reserved
		"reservations",
		"reserve",
		"permit", // special event permit application
		"application",
		"shipping", // shipping policy for tickets
		"policy",
		"policies",
		"package", // package includes
		"packages",
		"includes",
		"include",
		"including",
		"tweet",
		"print", // print entire month of events
		"entire",
		"month",
		"monthly",
		"21",
		"21+",
		"both",
		"nights",
		"box",
		"office",
		"this",
		"week",
		"tonight",
		"today",
		"http",
		"https",
		"open",
		"opens"
	};

	s_init3 = true;
	s_igt.set(8,0,512,s_igtbuf,10000,false,niceness,"igt-tab");
	int32_t n = (int32_t)sizeof(s_ignore)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char      *w    = s_ignore[i];
		int32_t       wlen = gbstrlen ( w );
		int64_t  h    = hash64Lower_utf8 ( w , wlen );
		if ( ! s_igt.addKey (&h) ) { char *xx=NULL;*xx=0; }
	}
}

// so Dates.cpp DF_FUZZY algorithm can see if the DT_YEAR date is in
// a mixed case and period-ending sentence, in which case it will consider
// it to be fuzzy since it is not header material
bool Sections::setSentFlagsPart1 ( ) {

	// shortcut
	wbit_t *bits = m_bits->m_bits;

	static int64_t h_i;
	static int64_t h_com;
	static int64_t h_org;
	static int64_t h_net;
	static int64_t h_pg;
	static int64_t h_pg13;
	static bool s_init38 = false;
	if ( ! s_init38 ) {
		s_init38 = true;
		h_i = hash64n("i");
		h_com = hash64n("com");
		h_org = hash64n("org");
		h_net = hash64n("net");
		h_pg  = hash64n("pg");
		h_pg13  = hash64n("pg13");
	}

	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		///////////////////
		//
		// SENT_MIXED_CASE
		//
		///////////////////
		si->m_sentFlags |= getMixedCaseFlags ( m_words ,
						       bits    ,
						       si->m_senta   ,
						       si->m_sentb   ,
						       m_niceness );


		bool firstWord = true;
		int32_t lowerCount = 0;
		for ( int32_t i = si->m_senta ; i < si->m_sentb ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// are we a stop word?
			bool isStopWord = m_words->isStopWord(i);
			// .com is stop word
			if ( m_wids[i] == h_com ||
			     m_wids[i] == h_org ||
			     m_wids[i] == h_net ||
			     // fixes mr. movie times "PG-13"
			     m_wids[i] == h_pg  ||
			     m_wids[i] == h_pg13 )
				isStopWord = true;
			// are we upper case?
			bool upper = is_upper_utf8(m_wptrs[i]) ;
			// . are we all upper case?
			// . is every single letter upper case?
			// . this is a good tie break sometimes like for
			//   the santafeplayhouse.org A TUNA CHRISTMAS
			//if ( megaCaps ) {
			//	if ( ! upper ) megaCaps = false;
			// allow if hyphen preceedes like for
			// abqfolkfest.org's "Kay-lee"
			if ( i>0 && m_wptrs[i][-1]=='-' ) upper = true;
			// if we got mixed case, note that!
			if ( m_wids[i] &&
			     ! is_digit(m_wptrs[i][0]) &&
			     ! upper &&
			     (! isStopWord || firstWord ) &&
			     // . November 4<sup>th</sup> for facebook.com
			     // . added "firstword" for "on AmericanTowns.com"
			     //   title prevention for americantowns.com
			     (m_wlens[i] >= 3 || firstWord) )
				lowerCount++;
			// no longer first word in sentence
			firstWord = false;
		}
		// does it end in period? slight penalty for that since
		// the ideal event title will not.
		// fixes events.kgoradio.com which was selecting the
		// first sentence in the description and not the performers
		// name for "Ragnar Bohlin" and "Malin Christennsson" whose
		// first sentence was for the most part properly capitalized
		// just by sheer luck because it used proper nouns and was
		// short.
		bool endsInPeriod = false;
		char *p = NULL;
		//if ( si->m_b < m_nw ) p = m_wptrs[si->m_b];
		int32_t lastPunct = si->m_sentb;
		// skip over tags to fix nonamejustfriends.com sentence
		for ( ; lastPunct < m_nw && m_tids[lastPunct] ; lastPunct++);
		// now assume, possibly incorrectly, that it is punct
		if ( lastPunct < m_nw ) p = m_wptrs[lastPunct];
		// scan properly to 
		char *send = p + m_wlens[lastPunct];
		char *s    = p;
		for ( ; s && s < send ; s++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// check. might have ". or ).
			if ( *s == '.' || 
			     *s == ';' ||
			     *s == '?' ||
			     *s == '!' ) {
				// do not count ellipsis for this though
				// to fix eventbrite.com 
				// "NYC iPhone Boot Camp: ..."
				if ( s[1] != '.' ) endsInPeriod = true;
				break;
			}
		}
		//if ( p && p[0] == '.' ) endsInPeriod = true;
		//if ( p && p[0] == '?' ) endsInPeriod = true;
		//if ( p && p[0] == '!' ) endsInPeriod = true;
		//if ( p && p[1] == '.' ) endsInPeriod = false; // ellipsis
		if ( isAbbr(m_wids[si->m_b-1]) && m_wlens[si->m_b-1]>1 ) 
			endsInPeriod = false;
		if ( m_wlens[si->m_b-1] <= 1 &&
		     // fix "world war I"
		     m_wids[si->m_b-1] != h_i )
			endsInPeriod = false;
		if ( endsInPeriod ) {
			si->m_sentFlags |= SENT_PERIOD_ENDS;
			// double punish if also has a lower case word
			// that should not be lower case in a title
			if ( lowerCount > 0 )
				si->m_sentFlags |= SENT_PERIOD_ENDS_HARD;
		}
	}
	return true;
}

#define METHOD_MONTH_PURE   0 // like "<h3>July</h3>"
#define METHOD_TAGID        1
#define METHOD_DOM          2
#define METHOD_ABOVE_DOM    3
#define METHOD_DOW          4
#define METHOD_DOM_PURE     5
#define METHOD_DOW_PURE     6
#define METHOD_ABOVE_DOW    7
#define METHOD_INNER_TAGID  8
#define METHOD_ABOVE_ADDR   9
#define METHOD_MAX          10

#define MAXCELLS (1024*5)

class Partition {
public:
	int32_t m_np;
	int32_t m_a[MAXCELLS];
	int32_t m_b[MAXCELLS];
	class Section *m_firstBro[MAXCELLS];
};

// . vec? now has dups!
float computeSimilarity2 ( int32_t   *vec0 , 
			  int32_t   *vec1 ,
			  int32_t   *s0   , // corresponding scores vector
			  int32_t   *s1   , // corresponding scores vector
			  int32_t    niceness ,
			  SafeBuf *pbuf ,
			  HashTableX *labelTable ,
			  int32_t nv0 ) {
	// if both empty, assume not similar at all
	if ( *vec0 == 0 && *vec1 == 0 ) return 0;
	// if either is empty, return 0 to be on the safe side
	if ( *vec0 == 0 ) return 0;
	if ( *vec1 == 0 ) return 0;

	HashTableX ht;
	char  hbuf[200000];
	int32_t  phsize = 200000;
	char *phbuf  = hbuf;
	// how many slots to allocate initially?
	int32_t need = 1024;
	if ( nv0 > 0 ) need = nv0 * 4;
	// do not use the buf on stack if need more...
	if ( need > 16384 ) { phbuf = NULL; phsize = 0; }
	// allow dups!
	if ( ! ht.set ( 4,4,need,phbuf,phsize,true,niceness,"xmlqvtbl2"))
		return -1;

	// for deduping labels
	HashTableX dedupLabels;
	if ( labelTable ) 
		dedupLabels.set(4,4,need,NULL,0,false,niceness,"xmldelab");

	bool useScores  = (bool)s0;

	int32_t matches    = 0;
	int32_t total      = 0;

	int32_t matchScore = 0;
	int32_t totalScore = 0;

	// hash first vector. accumulating score total and total count
	for ( int32_t *p = vec0; *p ; p++ , s0++ ) {
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s0;
		// total it up
		totalScore += score;
		// accumulate all the scores into this one bucket
		// in the case of p being a dup
		if ( ! ht.addTerm32 ( p , score ) ) return -1;
	}

	//int32_t zero = 0;

	// see what components of this vector match
	for ( int32_t *p = vec1; *p ; p++ , s1++ ) {
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s1;
		// and total scores
		totalScore += score;
		// is it in there?
		int32_t slot = ht.getSlot ( p );
		// skip if unmatched
		if ( slot < 0 ) {
			// skip if not debugging
			if ( ! pbuf ) continue;
			// get the label ptr
			char *pptr = (char *)labelTable->getValue((int32_t *)p);
			// record label in safeBuf if provided
			dedupLabels.addTerm32 ( (int32_t *)&pptr , score );
			//dedupLabels.addTerm32 ( (int32_t *)p );
			// and then skip
			continue;
		}

		// otherwise, it is a match!
		matches++;

		// and score of what we matched
		int32_t *val = (int32_t *)ht.getValueFromSlot ( slot );
		// sanity check. does "vec1" have dups in it? shouldn't...
		if ( *val == 0 ) { char *xx=NULL;*xx=0; }
		// we get the min
		int32_t minScore ;
		if ( *val < score ) minScore = *val;
		else                minScore = score;

		// only matched "min" of them times 2!
		matchScore += 2 * minScore;
		// he is hit too
		//matchScore += *val;
		// how many were unmatched?
		int32_t unmatched = *val + score - (2*minScore);

		// remove it as we match it to deal with dups
		// once we match it once, do not match again, score was
		// already accumulated
		// otherwise, remove this dup and try to match any
		// remaining dups in the table
		ht.setValue ( slot , &unmatched ); // &zero
		//ht.removeSlot ( slot );
	}

	// for debug add all remaining into dedup table
	for ( int32_t i = 0 ; labelTable && i < ht.getNumSlots(); i++ ) {
		QUICKPOLL(niceness);
		if ( ! ht.m_flags[i] ) continue;
		uint32_t *unmatched = (uint32_t *)ht.getValueFromSlot (i);
		if ( *unmatched == 0 ) continue;
		// use the key to get the label ptr
		int32_t key = *(int32_t *)ht.getKeyFromSlot(i);
		char *pptr = (char *)labelTable->getValue(&key);
		dedupLabels.addTerm32 ( (int32_t *)&pptr , *unmatched );
	}


	// if after subtracting query terms we got no hits, return 0.framesets?
	if ( useScores && totalScore == 0 ) return 0;
	if ( total                   == 0 ) return 0;
	// . what is the max possible score we coulda had?
	// . subtract the vector components that matched a query term
	float percent = 100 * (float)matchScore / (float)totalScore;
	// sanity
	if ( percent > 100 ) { char *xx=NULL;*xx=0; }

	if ( ! labelTable ) return percent;

	// scan label table for labels
	for ( int32_t i = 0 ; i < dedupLabels.getNumSlots(); i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip empties
		if ( ! dedupLabels.m_flags[i] ) continue;
		// get hash
		char **pptr = (char **)dedupLabels.getKeyFromSlot(i);
		// and count
		int32_t count = dedupLabels.getScoreFromSlot(i);
		// get label and count
		char *str = *pptr;//(char *)labelTable->getValue(&h);
		if ( count != 1 ) pbuf->safePrintf ( "%s(%"INT32") ",str,count);
		else              pbuf->safePrintf ( "%s ",str);
	}

	return percent;
}

char *getSentBitLabel ( sentflags_t sf ) {
	if ( sf == SENT_HAS_COLON  ) return "hascolon";
	if ( sf == SENT_AFTER_COLON  ) return "aftercolon";
	if ( sf == SENT_BAD_FIRST_WORD ) return "badfirstword";
	if ( sf == SENT_MIXED_CASE ) return "mixedcase";
	if ( sf == SENT_MIXED_CASE_STRICT ) return "mixedcasestrict";
	if ( sf == SENT_MULT_EVENTS ) return "multevents";
	if ( sf == SENT_PAGE_REPEAT ) return "pagerepeat";
	if ( sf == SENT_NUMBERS_ONLY ) return "numbersonly";
	if ( sf == SENT_SECOND_TITLE ) return "secondtitle";
	if ( sf == SENT_IS_DATE ) return "allwordsindate";
	if ( sf == SENT_LAST_STOP ) return "laststop";
	if ( sf == SENT_NUMBER_START ) return "numberstarts";
	if ( sf == SENT_IN_HEADER ) return "inheader";
	if ( sf == SENT_IN_LIST ) return "inlist";
	if ( sf == SENT_IN_BIG_LIST ) return "inbiglist";
	if ( sf == SENT_COLON_ENDS ) return "colonends";
	if ( sf == SENT_IN_TITLEY_TAG ) return "intitleytag";
	if ( sf == SENT_CITY_STATE ) return "citystate";
	if ( sf == SENT_PERIOD_ENDS ) return "periodends";
	if ( sf == SENT_HAS_PHONE ) return "hasphone";
	if ( sf == SENT_IN_MENU ) return "inmenu";
	if ( sf == SENT_MIXED_TEXT ) return "mixedtext";
	if ( sf == SENT_TAGS ) return "senttags";
	if ( sf == SENT_INTITLEFIELD ) return "intitlefield";
	if ( sf == SENT_STRANGE_PUNCT ) return "strangepunct";
	if ( sf == SENT_TAG_INDICATOR ) return "tagindicator";
	if ( sf == SENT_INNONTITLEFIELD ) return "innontitlefield";
	if ( sf == SENT_HASNOSPACE ) return "hasnospace";
	if ( sf == SENT_IS_BYLINE ) return "isbyline";
	if ( sf == SENT_NON_TITLE_FIELD ) return "nontitlefield";
	if ( sf == SENT_TITLE_FIELD ) return "titlefield";
	if ( sf == SENT_UNIQUE_TAG_HASH ) return "uniquetaghash";
	if ( sf == SENT_AFTER_SENTENCE ) return "aftersentence";
	if ( sf == SENT_WORD_SANDWICH ) return "wordsandwich";
	if ( sf == SENT_AFTER_SPACER ) return "afterspacer";
	if ( sf == SENT_BEFORE_SPACER ) return "beforespacer";
	if ( sf == SENT_NUKE_FIRST_WORD ) return "nukefirstword";
	if ( sf == SENT_FIELD_NAME ) return "fieldname";
	if ( sf == SENT_PERIOD_ENDS_HARD ) return "periodends2";
	if ( sf == SENT_PARENS_START ) return "parensstart";
	if ( sf == SENT_IN_MENU_HEADER ) return "inmenuheader";
	if ( sf == SENT_IN_TRUMBA_TITLE ) return "intrumbatitle";
	if ( sf == SENT_IN_TAG ) return "intag";
	if ( sf == SENT_MENU_SENTENCE ) return "menusentence";

	char *xx=NULL;*xx=0;
	return NULL;
}


// . PROBLEM: because we ignore non-breaking tags we often get sections
//   that are really not sentences, but we are forced into them because
//   we cannot split span or bold tags
//   i.e. "<div>This is <b>a sentence. And this</b> is a sentence.</div>"
//   forces us to treat the entire div tag as a sentence section.
// . i did add some logic to ignore those (the two for-k loops below) but then
//   Address.cpp cores because it expects every alnum word to be in a sentence
// . now make sure to shrink into our current parent if we would not lose
//   alnum chars!! fixes sentence flip flopping
// . returns false and sets g_errno on error
bool Sections::addSentenceSections ( ) {

	m_numSentenceSections = 0;

	sec_t badFlags = 
		//SEC_MARQUEE|
		SEC_STYLE|
		SEC_SCRIPT|
		SEC_SELECT|
		SEC_HIDDEN|
		SEC_NOSCRIPT;

	// shortcut
	Section **sp = m_sectionPtrs;

	static bool s_init = false;
	static int64_t h_in;
	static int64_t h_at;
	static int64_t h_for;
	static int64_t h_to;
	static int64_t h_on;
	static int64_t h_under;
	static int64_t h_with;
	static int64_t h_along;
	static int64_t h_from;
	static int64_t h_by;
	static int64_t h_of;
	static int64_t h_some;
	static int64_t h_the;
	static int64_t h_and;
	static int64_t h_a;
	static int64_t h_http;
	static int64_t h_https;
	static int64_t h_room;
	static int64_t h_rm;
	static int64_t h_bldg;
	static int64_t h_building;
	static int64_t h_suite;
	static int64_t h_ste;
	static int64_t h_tags;
	if ( ! s_init ) {
		s_init = true;
		h_tags = hash64n("tags");
		h_in = hash64n("in");
		h_the = hash64n("the");
		h_and = hash64n("and");
		h_a = hash64n("a");
		h_a = hash64n("a");
		h_at = hash64n("at");
		h_for = hash64n("for");
		h_to = hash64n("to");
		h_on = hash64n("on");
		h_under = hash64n("under");
		h_with = hash64n("with");
		h_along = hash64n("along");
		h_from = hash64n("from");
		h_by = hash64n("by");
		h_of = hash64n("of");
		h_some = hash64n("some");
		h_http = hash64n("http");
		h_https = hash64n("https");
		h_room = hash64n("room");
		h_rm = hash64n("rm");
		h_bldg = hash64n("bldg");
		h_building = hash64n("building");
		h_suite = hash64n("suite");
		h_ste = hash64n("ste");
	}

	// need D_IS_IN_URL bits to be valid
	m_bits->setInUrlBits ( m_niceness );
	// shortcut
	wbit_t *bb = m_bits->m_bits;

	// is the abbr. a noun? like "appt."
	bool hasWordAfter = false;

	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need a wid
		if ( ! m_wids[i] ) continue;
		// get section we are currently in
		Section *cs = m_sectionPtrs[i];
		// skip if its bad! i.e. style or script or whatever
		if ( cs->m_flags & badFlags ) continue;
		// set that
		int64_t prevWid = m_wids[i];
		int64_t prevPrevWid = 0LL;
		// flag
		int32_t lastWidPos = i;//-1;
		bool lastWasComma = false;
		nodeid_t includedTag = -2;
		int32_t lastbr = -1;
		bool endOnBr = false;
		bool endOnBold = false;
		bool capped = true;
		int32_t upper = 0;
		int32_t numAlnums = 0;
		int32_t verbCount = 0;
		// scan for sentence end
		int32_t j; for ( j = i ; j < m_nw ; j++ ) {
			// skip words
			if ( m_wids[j] ) {
				// prev prev
				prevPrevWid = prevWid;
				// assume not a word like "vs."
				hasWordAfter = false;
				// set prev
				prevWid = m_wids[j];
				lastWidPos = j;
				lastWasComma = false;
				endOnBr = false;
				endOnBold = false;
				numAlnums++;
				// skip if stop word and need not be
				// capitalized
				if ( bb[j] & D_IS_STOPWORD ) continue;
				if ( m_wlens[j] <= 1 ) continue;
				if ( is_digit(m_wptrs[j][0]) ) continue;
				if ( !is_upper_utf8(m_wptrs[j])) capped=false;
				else                           upper++;
				// is it a verb?
				if ( isVerb( &m_wids[j] ) )
					verbCount++;
				//if ( bb[i] & D_IS_IN_DATE_2 ) inDate = true;
				// it is in the sentence
				continue;
			}
			// tag?
			if ( m_tids[j] ) {
				// shortcut
				nodeid_t tid = m_tids[j] & BACKBITCOMP;

				// treat nobr as breaking to fix ceder.net
				// which has it after the group title
				if ( tid == TAG_NOBR ) break;

				if ( tid == TAG_BR ) endOnBr = true;
				if ( tid == TAG_B  ) endOnBold = true;

				// a </b><br> is usually like a header
				if ( capped && upper && endOnBr && endOnBold )
					break;
				// if it is <span style="display:none"> or
				// div or whatever, that is breaking!
				// fixes http://chuckprophet.com/gigs/ 
				if ( (tid == TAG_DIV ||
				      tid == TAG_SPAN ) &&
				     m_wlens[j] > 14 &&
				     strncasestr(m_wptrs[j],"display:none",
						 m_wlens[j]) )
					break;
				// ok, treat span as non-breaking for a second
				if ( tid == TAG_SPAN ) continue;
				// mark this
				if ( tid == TAG_BR ) lastbr = j;
				//
				// certain tags like span and br sometimes
				// do and sometimes do not break a sentence.
				// so by default assume they do, but check
				// for certain indicators...
				//
				if ( tid == TAG_SPAN || 
				     tid == TAG_BR   ||
				     // fixes guysndollsllc.com:
				     // causes core dump:
				     tid == TAG_P    || // villr.com
				     // fixes americantowns.com
				     tid == TAG_DIV     ) {
					// if nothing after, moot point
					if ( j+1 >= m_nw ) break;
					// if we already included this tag
					// then keep including it. but some
					// span tags will break and some won't
					// even when in or around the same
					// sentence. see that local.yahoo.com
					// food delivery services url for
					// the first street address, 
					// 5013 Miramar
					if ( includedTag == tid &&
					     (m_tids[j] & BACKBIT) ) {
						// reset it in case next
						// <span> tag is not connective
						includedTag = -2;
						continue;
					}
					// if we included this tag type
					// as a front tag, then include its
					// back tag in sentence as well.
					// fixes nonamejustfriends.com
					// which has a span tag in sentence:
					// ".. Club holds a <span>FREE</span> 
					//  Cruise Night..." and we allow
					// "<span>" because it follows "a",
					// but we were breaking on </span>!
					if ( !(m_tids[j]&BACKBIT))
						includedTag = tid;
					// if prev punct was comma and not
					// an alnum word
					if ( lastWasComma ) continue;
					// get punct words bookcasing this tag
					if ( ! m_wids[j+1] && 
					     ! m_tids[j+1] &&
					     m_words->hasChar(j+1,',') )
						continue;
					// if prevwid is like "vs." then
					// that means keep going even if
					// we hit one of these tags. fixes
					// "new york knicks vs.<br>orlando
					//  magic"
					if ( hasWordAfter )
						continue;
					// if first alnum word after tag
					// is lower case, that is good too
					int32_t aw = j + 1;
					int32_t maxaw = j + 12;
					if ( maxaw > m_nw ) maxaw = m_nw;
					for ( ; aw < maxaw ; aw++ )
						if ( m_wids[aw] ) break;
					bool isLower = false;
					if ( aw < maxaw &&
					     is_lower_utf8(m_wptrs[aw]) ) 
						isLower = true;

					// http or https is not to be
					// considered as such! fixes
					// webnetdesign.com from getting
					// sentences continued by an http://
					// url below them.
					if ( aw < maxaw &&
					     (m_wids[aw] == h_http ||
					      m_wids[aw] == h_https) )
						isLower = false;

					// this almost always breaks a sentence
					// and adding this line here fixes
					// "Sunday<p>noon" the thewoodencow.com
					// and let's villr.com stay the same
					// since its first part ends in "and"
					//if ( m_wids[aw] == h_noon ||
					//     m_wids[aw] == h_midnight )
					if ( tid == TAG_P &&
					     isLower &&
					     // Oscar G<p>along with xxxx
					     m_wids[aw] != h_along &&
					     m_wids[aw] != h_with )
						isLower = false;

					if ( isLower ) continue;
					// get pre word, preopsitional
					// phrase starter?
					if ( prevWid == h_in ||
					     prevWid == h_the ||
					     prevWid == h_and ||
					     // fix for ending on "(Room A)"
					     (prevWid == h_a &&
					      prevPrevWid != h_rm &&
					      prevPrevWid != h_room &&
					      prevPrevWid != h_bldg &&
					      prevPrevWid != h_building &&
					      prevPrevWid != h_suite &&
					      prevPrevWid != h_ste ) ||
					     prevWid == h_for ||
					     prevWid == h_to ||
					     prevWid == h_on ||
					     prevWid == h_under ||
					     prevWid == h_with ||
					     prevWid == h_from ||
					     prevWid == h_by ||
					     prevWid == h_of ||
					     // "some ... Wednesdays"
					     prevWid == h_some ||
					     prevWid == h_at )
						continue;
				}


				// seems like span breaks for meetup.com
				// et al and not for abqtango.com maybe, we
				// need to download the css??? or what???
				// by default span tags do not seem to break
				// the line but ppl maybe configure them to
				if ( tid == TAG_SPAN ) break;
				// if like <font> ignore it
				if ( ! isBreakingTagId(m_tids[j]) ) continue;
				// only break on xml tags if in rss feed to
				// fix <st1:State w:st="on">Arizona</st1>
				// for gwair.org
				if ( tid==TAG_XMLTAG && !m_isRSSExt) continue;
				// otherwise, stop!
				break;
			}
			// skip simple spaces for speed
			if ( m_wlens[j] == 1 && is_wspace_a(m_wptrs[j][0]))
				continue;

			// do not allow punctuation that is in a url
			// to be split up or used as a splitter. we want
			// to keep the full url intact.
			if ( j > i && j+1 < m_nw &&
			     (bb[j-1] & D_IS_IN_URL) &&
			     (bb[j  ] & D_IS_IN_URL) &&
			     (bb[j+1] & D_IS_IN_URL) )
				continue;

			// was last punct containing a comma?
			lastWasComma = false;
			// scan the punct chars, stop if we hit a sent breaker
			char *p    =     m_wptrs[j];
			char *pend = p + m_wlens[j];
			for ( ; p < pend ; p++ ) {
				// punct word...
				if ( *p == '.' ) break;
				if ( *p == ',' ) lastWasComma =true;
				// allow this too for now... no...
				if ( *p == ';' ) break;
				// now hyphen breaks, mostly for stuff
				// in title tags like dukecityfix.com
				if ( sp[j]->m_tagId == TAG_TITLE &&
				     *p == '-' &&
				     is_wspace_a(p[-1]) &&
				     is_wspace_a(p[+1]) &&
				     lastWidPos >= 0 &&
				     ! m_isRSSExt &&
				     j+1<m_nw &&
				     m_wids[j+1] &&
				     //( ! (bb[lastWidPos] & D_IS_IN_DATE) ||
				     //  ! (bb[j+1] & D_IS_IN_DATE)       ) &&
				     // fix for $10 - $12
				     ( ! is_digit ( m_wptrs[lastWidPos][0]) ||
				       ! is_digit ( m_wptrs[j+1][0]) ) )
					break;
				// . treat colon like comma now
				// . for unm.edu we have 
				//   "Summer Hours: March 15 - Oct15:
				//    8 am. Mon - Fri, 7:30 am - 10 am Sun.,
				//    Winter Hours: Oct. 15 - March 15:
				//    8 am., seven days a week"
				// . and we don't want "winter hours" being
				//   toplogically closer to the summer hours
				// . that is, the colon is a stronger binder
				//   than the comma?
				// . but for villr.com Hours: May-Aug.. gets
				//   made into two sentences and Hours is
				//   seen as a heading section and causes
				//   addImpliedSections() to be wrong.
				//if ( *p == ':' ) lastWasComma =true;
				// . why not the colon?
				if ( *p == ':' ) {

					// Tags: music,concert,fun
					if ( prevWid == h_tags &&
					     // just Tags: so far in sentence
					     j == i )
						break;

					// a "::" is used in breadcrumbs,
					// so break on that.
					// fixes "Dining :: Visit :: 
					// Cal Performacnes" title
					if ( p[1] == ':' ) 
						break;

					// if "with" preceeds, allow
					if ( prevWid == h_with ) continue;

					// or prev word was tag! like
					// "blah</b>:..."
					bool tagAfter=(j-1>=0&&m_tids[j-1]);

					// do not allow if next word is tag
					bool tagBefore=(j+1<m_nw&&m_tids[j+1]);

					// do not allow 
					// "<br>...:<br>" or
					// "<br>...<br>:" or
					// since such things are usually
					// somewhat like headers. isolated
					// lines ending on a colon.
					// should fix st. martin's center
					// for unm.edu "Summer Hours: ..."
					if ( lastbr >= 0 && 
					     ( tagBefore || tagAfter ) ) {
						// end sentence there then
						j = lastbr;
						break;
					}
					     
					if ( tagBefore ) break;
					if ( tagAfter  ) break;

					// for now allow it!
					continue;
				}
				// . special hyphen
				// . breaks up title for peachpundit.com
				//   so we get better event title generation
				//   since peachpundit.com will be a reepat sec
				// . BUT it did not work!
				if ( p[0] == (char)-30 &&
				     p[1] == (char)-128 &&
				     p[2] == (char)-108 )
					break;
				// this for sure
				// "Home > Albuquerque Events > Love Song ..."
				if ( *p == '>' ) break;
				if ( *p == '!' ) break;
				if ( *p == '?' ) break;
				if ( *p == '|' ) 
					break;
				// bullets
				if ( p[0] == (char)226 &&
				     p[1] == (char)128 &&
				     p[2] == (char)162 )
					break;
			redo:
				continue;
			}
			// if none, keep going
			if ( p == pend ) continue;
			// if an alnum char follows the ., it is ok
			// probably a hostname or ip or phone #
			if ( is_alnum_utf8(p+1) &&
			     // "venue:ABQ Sq Dance Center..." for
			     // americantowns.com has no space after the colon!
			     *p !=':' ) 
				goto redo;
			// if abbreviation before we are ok too
			if ( *p == '.' && isAbbr(prevWid,&hasWordAfter) ) {
				// but the period may serve a double purpose
				// to end the abbr and terminate the sentence
				// if the word that follows is capitalized,
				// and if the abbr is a lower-case noun.
				//
				// if abbr is like "vs" then do not end sentenc
				if ( hasWordAfter )
					goto redo;

				// set "next" to next alnum word after us
				int32_t next = j+1;
				int32_t max  = next + 10;
				if ( max > m_nw ) max = m_nw;
				for ( ; next < max ; next++ ) {
					if ( ! m_wids[next] ) continue;
					break;
				}

				// was previous word/abbr capitalized?
				// if so, assume period does not end sentence.
				if ( m_words->isCapitalized(lastWidPos) )
					goto redo;
				// if next word is NOT capitalized, assume
				// period does not end sentence...
				if ( next < max &&
				     ! m_words->isCapitalized ( next ) )
					goto redo;
				// otherwise, abbr is NOT capitalized and
				// next word IS capitalized, so assume the
				// period does NOT end the sentence
			}
			// fix "1. library name" for cabq.gov
			if ( *p == '.' && 
			     lastWidPos == i &&
			     m_words->isNum(lastWidPos) )
				goto redo;
			// ok, stop otherwise
			break;
		}

		// do not include tag at end. try to fix sentence flip flop.
		for ( ; j > i ; j-- ) 
			// stop when we just contain the last word
			if ( m_wids[j-1] ) break;

		// make our sentence endpoints now
		int32_t senta = i;
		// make the sentence defined by [senta,sentb) where sentb
		// defines a half-open interval like we do for almost 
		// everything else
		int32_t sentb = j;

		// update i for next iteration
		i = sentb - 1;

		// crap, but now sentences intersect with our tag-based
		// sections because they can now split tags because of websites
		// like aliconference.com and abqtango.com whose sentences
		// do not align with the tag sections. therefore we introduce
		// the SEC_TOP_SPLIT and SEC_BOTTOM_SPLIT to indicate 
		// that the section is a top/bottom piece of a split sentence.
		// if both bits are set we assume SEC_MIDDLE_SPLIT.
		// then we set the Section::m_senta and m_sentb to
		// indicate the whole sentence of which it is a split.
		// but the vast majority of the time m_senta and m_sentb
		// will equal m_firstWordPos and m_lastWordPos respectively.
		// then, any routine that


		// so scan the words in the sentence and as we scan we have
		// to determine the parent section we inserting the sentence
		// into as a child section.
		//Section *parent = NULL;
		int32_t     start  = -1;
		Section *pp;
		//Section *np;
		int32_t     lastk = 0;
		Section *splitSection = NULL;
		Section *lastGuy = NULL;

		for ( int32_t k = senta ; k <= sentb ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// add final piece
			if ( k == sentb ) {
				// stop i no final piece
				if ( start == -1 ) break;
				// otherwise, add it
				goto addit;
			}
			// need a real alnum word
			if ( ! m_wids[k] ) continue;
			// get his parent
			pp = m_sectionPtrs[k];
			// set parent if need to
			//if ( ! parent ) parent = pp;
			// and start sentence if need to
			if ( start == -1 ) start = k;
			// if same as exact section as last guy, save some time
			if ( pp == lastGuy ) pp = NULL;
			// store it
			lastGuy = pp;
			// . i'd say blow up "pp" until its contains "start"
			// . but if before it contains start it breaches
			//   [senta,sentb) then we have to cut things short
			for ( ; pp ; pp = pp->m_parent ) {
				// breathe
				QUICKPOLL(m_niceness);
				// we now have to split section "pp"
				// when adding the sentence section.
				// once we have such a section we
				// cannot use a different parent...
				if ( pp->m_firstWordPos < start ||
				     pp->m_lastWordPos >= sentb ) {
					// set it
					if ( ! splitSection ) splitSection =pp;
					// WE ARE ONLY ALLOWED TO SPLIT ONE
					// SECTION ONLY...
					if ( pp != splitSection)
						goto addit;
					break;
				}
				// keep telescoping until "parent" contains
				// [senta,k] , and we already know that it
				// contains k because that is what we set it to
				//if ( pp->m_a <= senta ) break;
			}
			// mark it
			if ( m_wids[k] ) lastk = k;
			// ok, keep chugging
			continue;

			// add the final piece if we go to this label
		addit:
			// use this flag
			int32_t bh = BH_SENTENCE;
			// determine parent section, smallest section 
			// containing [start,lastk]
			Section *parent = m_sectionPtrs[start];
			for ( ; parent ; parent = parent->m_parent ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if contains lastk
				if ( parent->m_b > lastk ) break;
			}
			// 
			// for "<span>Albuquerque</span>, New Mexico" 
			// "start" points to "Albuquerque" but needs to 
			// point to the "<span>" so its parent is "parent"
			int32_t adda = start;
			int32_t addb = lastk;
			// need to update "start" to so its parent is the new 
			// "parent" now so insertSubSection() does not core
			for ( ; adda >= 0 ; ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if we finally got the right parent
				if ( m_sectionPtrs[adda]==parent ) break;
				// or if he's a tag and his parent
				// is "parent" we can stop.
				// i.e. STOP on a proper subsection of
				// the section containing the sentence.
				if ( m_sectionPtrs[adda]->m_parent==parent &&
				     m_sectionPtrs[adda]->m_a == adda )
					break;
				// backup
				adda--;
				// check
				if ( adda < 0 ) break;
				// how can this happen?
				if ( m_wids[adda] ) { char *xx=NULL;*xx=0; }
			}
			// sanity
			if ( adda < 0 ) { char *xx=NULL;*xx=0; }

			// backup addb over any punct we don't need that
			//if ( addb > 0 && addb < m_nw && 
			//     ! m_wids[addb] && ! m_tids[addb] ) addb--;

			// same for right endpoint
			for ( ; addb < m_nw ; ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if we finally got the right parent
				if ( m_sectionPtrs[addb]==parent ) break;
				// get it
				Section *sp = m_sectionPtrs[addb];
				// come back up here in the case of a section
				// sharing its Section::m_b with its parent
			subloop:
				// or if he's a tag and his parent
				// is "parent" we can stop
				if ( sp->m_parent==parent &&
				     sp->m_b == addb+1 )
					break;
				// or if we ran into a brother section
				// that does not contain the sentence...
				// fix core dump for webnetdesign.com whose
				// sentence consisted of 3 sections from
				// A=7079 to B=7198. but now i am getting rid
				// of allowing a lower case http(s):// on
				// a separate line to indicate that the
				// sentence continues... so we will not have
				// this sentence anymore in case  you are
				// wondering why it is not there any more.
				if ( sp->m_parent==parent &&
				     sp->m_a == addb ) {
					// do not include that brother's tag
					addb--;
					break;
				}

				// when we have bad tag formations like for
				// http://gocitykids.parentsconnect.com/catego
				// ry/buffalo-ny-usa/places-to-go/tourist-stops
				// like <a><b>...</div> with no ending </a> or
				// </b> tags then we have to get the parent
				// of the parent as int32_t as its m_b is the
				// same and check that before advancing addb
				// otherwise we can miss the parent section
				// that we want! (this is because the kid
				// sections share the same m_b as their 
				// parent because of they have no ending tag)
				if ( sp->m_parent &&
				     sp->m_parent->m_b == sp->m_b ) {
					sp = sp->m_parent;
					goto subloop;
				}

				// advance
				addb++;
				// stop if addb 
				if ( addb >= m_nw ) break;
				// how can this happen?
				if ( m_wids[addb] ) { char *xx=NULL;*xx=0; }
			}
			// sanity
			if ( addb >= m_nw ) { char *xx=NULL;*xx=0; }

			// ok, now add the split sentence
			Section *is =insertSubSection(adda,addb+1,bh);
			// panic?
			if ( ! is ) return false;
			// set sentence flag on it
			is->m_flags |= SEC_SENTENCE;
			// count it
			m_numSentenceSections++;
			// print it out
			/*
			SafeBuf tt;
			tt.safeMemcpy(m_wptrs[adda],
				      m_wptrs[addb]+m_wlens[addb]-
					      m_wptrs[adda]);
			tt.safeReplace2 ( "\n",1,"*",1,m_niceness);
			tt.safeReplace2 ( "\r",1,"*",1,m_niceness);
			if ( is->m_flags & SEC_SPLIT_SENT )
				tt.safePrintf(" [split]");
			tt.pushChar(0);
			fprintf(stderr,"a=%"INT32" %s\n",start,tt.m_buf);
			*/
			// . set this
			// . sentence is from [senta,sentb)
			is->m_senta = senta;//start;
			is->m_sentb = sentb;//k;
			// use this too if its a split of a sentence
			if ( is->m_senta < is->m_a ) 
				is->m_flags |= SEC_SPLIT_SENT;
			if ( is->m_sentb > is->m_b ) 
				is->m_flags |= SEC_SPLIT_SENT;
			// stop if that was it
			if ( k == sentb ) break;
			// go on to next fragment then
			start = -1;
			parent = NULL;
			splitSection = NULL;
			lastGuy = NULL;
			// redo this same k
			k--;
		}
	}

	int32_t     inSentTil = 0;
	Section *lastSent = NULL;
	// get the section of each word. if not a sentence section then
	// make its m_sentenceSection point to its parent that is a sentence
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need sentence
		if ( ( sk->m_flags & SEC_SENTENCE ) ) {
			inSentTil = sk->m_b;
			lastSent  = sk;
			sk->m_sentenceSection = sk;
			continue;
		}
		// skip if outside of the last sentence we had
		if ( sk->m_a >= inSentTil ) continue;
		// we are in that sentence
		sk->m_sentenceSection = lastSent;
	}

	return true;
}

Section *Sections::insertSubSection ( int32_t a, int32_t b, int32_t newBaseHash ) {
	// debug
	//log("sect: inserting subsection [%"INT32",%"INT32")",a,b);

	// try to realloc i guess. should keep ptrs in tact.
	if ( m_numSections >= m_maxNumSections )
		// try to realloc i guess
		if ( ! growSections() ) return NULL;
		//char *xx=NULL;*xx=0;}

	//
	// make a new section
	//
	Section *sk = &m_sections[m_numSections];
	// clear
	memset ( sk , 0 , sizeof(Section) );
	// inc it
	m_numSections++;
	// now set it
	sk->m_a   = a;
	sk->m_b   = b;

	// don't mess this up!
	if ( m_lastSection && a > m_lastSection->m_a )
		m_lastSection = sk;

	// the base hash (delimeter hash) hack
	sk->m_baseHash = 0;// dh; ????????????????????

	// do not resplit this split section with same delimeter!!
	sk->m_processedHash = 0; // ?????? dh;

	// get first section containing word #a
	Section *si = m_sectionPtrs[a];

	for ( ; si ; si = si->m_prev ) {
		// breathe
		QUICKPOLL(m_niceness);

		// we become his child if this is true
		if ( si->m_a < a ) {
			break;
		}

		// if he is bigger (or equal) we become his child
		// and are after him
		if ( si->m_a == a && si->m_b >= b ) {
			break;
		}
	}

	// . try using section before us if it is contained by "si"
	// . like in the case when word #a belongs to the root section
	//   and there are thousands of child sections of the root before "a"
	//   we really want to get the child section of the root before us
	//   as the prev section, "si", otherwise the 2nd for loop below here
	//   will hafta loop through thousands of sibling sections
	// . this will fail if word before a is part of our same section
	// . what if we ignored this for now and set m_sectionPtrs[a] to point
	//   to the newly inserted section, then when done adding sentence
	//   sections we scanned all the words, keeping track of the last
	//   html section we entered and used that to insert the sentence sections
	if ( m_lastAdded && m_lastAdded->m_a > si->m_a && m_lastAdded->m_a < a ) {
		si = m_lastAdded;
	}


	// crap we may have 
	// "<p> <strong>hey there!</strong> this is another sentence.</p>"
	// then "si" will be pointing at the "<p>" section, and we will
	// not get the "<strong>" section as the "prev" to sk, which we should!
	// that is where sk is the "this is another sentence." sentence
	// section. so to fix that try iterating over si->m_next to get si to
	// be closer to sk.
	for ( ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if no more eavailable
		if ( ! si->m_next ) break;
		// stop if would break
		if ( si->m_next->m_a > a ) break;
		// if it gets closer to us without exceeding us, use it
		if ( si->m_next->m_a < a ) continue;
		// if tied, check b. if it contains us, go to it
		if ( si->m_next->m_b >= b ) continue;
		// otherwise, stop
		break;
	}

	// set this
	m_lastAdded = si;

	// a br tag can split the very first base html tag like for
	// mapsandatlases.org we have
	// "<html>...</html> <br> ...." so the br tag splits the first
	// section!
	// SO we need to check for NULL si's!
	if ( ! si ) {
		// skip this until we figure it out
		m_numSections--;
		char *xx=NULL;*xx=0;
		return NULL;
//		sk->m_next = m_rootSection;//m_rootSection;
//		sk->m_prev = NULL;
//		//m_sections[0].m_prev = sk;
//		m_rootSection->m_prev = sk;
//		m_rootSection = sk;
	} else {
		// insert us into the linked list of sections
		if ( si->m_next ) si->m_next->m_prev = sk;
		sk->m_next   = si->m_next;
		sk->m_prev   = si;
		si->m_next   = sk;
	}

	// now set the parent
	Section *parent = m_sectionPtrs[a];
	// expand until it encompasses both a and b
	for ( ; ; parent = parent->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( parent->m_a > a ) continue;
		if ( parent->m_b < b ) continue;
		break;
	}
	// now we assign the parent to you
	sk->m_parent    = parent;
	sk->m_exclusive = parent->m_exclusive;
	// sometimes an implied section is a subsection of a sentence!
	// like when there are a lot of brbr (double br) tags in it...
	sk->m_sentenceSection = parent->m_sentenceSection;
	// take out certain flags from parent
	sec_t flags = parent->m_flags;
	// like this
	flags &= ~SEC_SENTENCE;
	flags &= ~SEC_SPLIT_SENT;
	// but take out unbalanced!
	flags &= ~SEC_UNBALANCED;

	// . remove SEC_HAS_DOM/DOW/TOD 
	// . we scan our new kids for reparenting to us below, so OR these
	//   flags back in down there if we should
	flags &= ~SEC_HAS_DOM;
	flags &= ~SEC_HAS_DOW;
	flags &= ~SEC_HAS_TOD;
	//flags &= ~SEC_HAS_DATE;

	// add in fake
	flags |= SEC_FAKE;
	// flag it as a fake section
	sk->m_flags = flags ;
	// need this
	sk->m_baseHash = newBaseHash;

	// reset these
	sk->m_firstWordPos = -1;
	sk->m_lastWordPos  = -1;
	sk->m_alnumPosA    = -1;
	sk->m_alnumPosB    = -1;
	sk->m_senta        = -1;
	sk->m_sentb        = -1;
	sk->m_headColSection = NULL;
	sk->m_headRowSection = NULL;
	sk->m_tableSec       = NULL;
	sk->m_rowNum         = 0;
	sk->m_colNum         = 0;


#ifdef _DEBUG_SECTIONS_
	// interlaced section detection
	if ( m_isTestColl ) {
		// scan from words and telescope up
		Section *s1 = m_sectionPtrs[a];
		Section *s2 = m_sectionPtrs[b-1];
		// check for interlace
		for ( ; s1 ; s1 = s1->m_parent ) 
			if ( s1->m_a < a && 
			     s1->m_b > a &&
			     s1->m_b < b    ) {char *xx=NULL;*xx=0;}
		// check for interlace
		for ( ; s2 ; s2 = s2->m_parent ) 
			if ( s2->m_a < b && 
			     s2->m_b > b &&
			     s2->m_a > a    ) {char *xx=NULL;*xx=0;}
	}
#endif

	// try to keep a sorted linked list
	//Section *current = m_sectionPtrs[a];

	//return sk;

	// for inheriting flags from our kids
	sec_t mask = (SEC_HAS_DOM|SEC_HAS_DOW|SEC_HAS_TOD);//|SEC_HAS_DATE);

	//
	// !!!!!!!!!! SPEED THIS UP !!!!!!!!!!
	//

	// . find any child section of "parent" and make us their parent
	// . TODO: can later speed up with ptr to ptr logic
	// . at this point sections are not sorted so we can't
	//   really iterate linearly through them ... !!!
	// . TODO: speed this up!!!
	//
	// . TODO: use hashtable?
	// . TODO: aren't these sections in order by m_a??? could just use that
	//
	//for ( int32_t xx = 0 ; xx < m_numSections ; xx++ ) {

	// set sk->m_firstWordPos
	for ( int32_t i = a ; i < b ; i++ ) {
		// and first/last word pos
		if ( ! m_wids[i] ) continue;
		// mark this
		sk->m_firstWordPos = i;
		break;
	}

	// set sk->m_lastWordPos
	for ( int32_t i = b-1 ; i >= a ; i-- ) {
		// and first/last word pos
		if ( ! m_wids[i] ) continue;
		// mark this
		sk->m_lastWordPos = i;
		break;
	}


	//
	// to speed up scan the words in our inserted section, usually
	// a sentence section i guess, because our parent can have a ton
	// of children sections!!
	//
	for ( int32_t i = a ; i < b ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get current parent of that word
		Section *wp = m_sectionPtrs[i];
		// if sentence section does NOT contain the word's current
		// section then the sentence section becomes the new section
		// for that word.
		if ( ! sk->strictlyContains ( wp ) ) {
			// now if "wp" is like a root, then sk becomes the kid
			m_sectionPtrs[i] = sk;
			// our parent is wp
			sk->m_parent = wp;
			continue;
		}
		// we gotta blow up wp until right before it is bigger
		// than "sk" and use that
		for ( ; wp->m_parent ; wp = wp->m_parent )
			// this could be equal to, not just contains
			// otherwise we use strictlyContains()
			if ( wp->m_parent->contains(sk) ) break;
		// already parented to us?
		if ( wp->m_parent == sk ) continue;
		// sentence's parent is now wp's parent
		sk->m_parent = wp->m_parent;
		// and we become wp's parent
		wp->m_parent = sk;
		// and or his flags into us. SEC_HAS_DOM, etc.
		sk->m_flags |= wp->m_flags & mask;
		// sanity check
		if ( wp->m_b > sk->m_b ) { char *xy=NULL;*xy=0; }
		if ( wp->m_a < sk->m_a ) { char *xy=NULL;*xy=0; }
	}

	return sk;
}

// for brbr and hr splitting delimeters
int32_t Sections::splitSectionsByTag ( nodeid_t tagid ) {

	// . try skipping for xml
	// . eventbrite.com has a bunch of dates per event item and
	//   we end up using METHOD_DOM on those!
	// . i think the implied section algo is meant for html really
	//   or plain text
	if ( m_contentType == CT_XML &&
	     ( m_isEventBrite ||
	       m_isStubHub    ||
	       m_isFacebook ) )
		return 0;

	int32_t numAdded = 0;
	// . now, split sections up if they contain one or more <hr> tags
	// . just append some "hr" sections under that parent to m_sections[]
	// . need to update m_sectionPtrs[] after this of course!!!!!
	// . now we also support various other delimeters, like bullets
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be brbr or hr
		if ( ! isTagDelimeter ( si , tagid ) ) continue;
		// must have a next brother to be useful
		if ( ! si->m_nextBrother ) continue;
		// skip if already did this section
		if ( si->m_processedHash ) continue;
		// set first brother
		Section *first = si;
		for ( ; first->m_prevBrother ; first = first->m_prevBrother )
			// breathe
			QUICKPOLL(m_niceness);

	subloop:
		// mark it
		first->m_processedHash = 1;

		// start of insertion section is right after tag
		int32_t a = first->m_b;

		// but if first is not a tag delimeter than use m_a
		if ( ! isTagDelimeter ( first , tagid ) ) a = first->m_a;

		// or if first section has text, then include that, like
		// in the case of h1 tags for example
		if ( first->m_firstWordPos >= 0 ) a = first->m_a;

		// end of inserted section is "b"
		int32_t b = -1;

		int32_t numTextSections = 0;
		// count this
		if ( first->m_firstWordPos >= 0 ) numTextSections++;
		// start scanning right after "first"
		Section *last = first->m_nextBrother;
		// set last brother and "b"
		for ( ; last ; last = last->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop on tag delimeters
			if ( isTagDelimeter ( last , tagid ) ) {
				// set endpoint of new subsection
				b = last->m_a;
				// and stop
				break;
			}
			// assume we are the end of the line
			b = last->m_b;
			// count this
			if ( last->m_firstWordPos >= 0 ) numTextSections++;
		}

		// . insert [first->m_b,b]
		// . make sure it covers at least one "word" which means
		//   that a != b-1
		if ( a < b - 1 &&
		     // and must group together something meaningful
		     numTextSections >= 2 ) {
			// do the insertion
			Section *sk = insertSubSection (a,b,BH_IMPLIED);
			// error?
			if ( ! sk ) return -1;
			// fix it
			sk->m_processedHash = 1;
			// count it
			numAdded++;
		}

		// first is now last
		first = last;
		// loop up if there are more brothers
		if ( first ) goto subloop;
	}
	return numAdded;
}

bool Sections::splitSections ( char *delimeter , int32_t dh ) {

	// . try skipping for xml
	// . eventbrite.com has a bunch of dates per event item and
	//   we end up using METHOD_DOM on those!
	// . i think the implied section algo is meant for html really
	//   or plain text
	if ( m_contentType == CT_XML &&
	     ( m_isEventBrite ||
	       m_isStubHub    ||
	       m_isFacebook ) )
		return 0;

	int32_t saved = -1;
	int32_t delimEnd = -1000;

	// . now, split sections up if they contain one or more <hr> tags
	// . just append some "hr" sections under that parent to m_sections[]
	// . need to update m_sectionPtrs[] after this of course!!!!!
	// . now we also support various other delimeters, like bullets
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		if ( i < saved ) { char *xx=NULL;*xx=0; }
		// a quicky
		if ( ! isDelimeter ( i , delimeter , &delimEnd ) ) continue;
		// get section it is in
		Section *sn = m_sectionPtrs[i];

		// skip if already did this section
		if ( sn->m_processedHash == dh ) continue;

		// what section # is section "sn"?
		int32_t offset = sn - m_sections;

		// sanity check
		if ( &m_sections[offset] != sn ) { char *xx=NULL;*xx=0; }

		// init this
		int32_t start = sn->m_a;
		// CAUTION: sn->m_a can equal "i" for something like:
		// "<div><h2>blah</h2> <hr> </div>"
		// where when splitting h2 sections we are at the start
		// of an hr section. i think its best to just skip it!
		// then if we find another <h2> within that same <hr> section
		// it can split it into non-empty sections
		if ( start == i ) continue;

		// save it so we can rescan from delimeter right after this one
		// because there might be more delimeters in DIFFERENT 
		// subsections
		saved = i;

	subloop:
		// sanity check
		if ( m_numSections >= m_maxNumSections) {char *xx=NULL;*xx=0;}
		//
		// try this now
		//
		Section *sk = insertSubSection ( start , i , dh );

		// do not resplit this split section with same delimeter!!
		if ( sk ) sk->m_processedHash = dh;

		// if we were it, no more sublooping!
		if ( i >= sn->m_b ) { 
			// sn loses some stuff
			sn->m_exclusive = 0;
			// resume where we left off in case next delim is
			// in a section different than "sn"
			i = saved; 
			// do not process any more delimeters in this section
			sn->m_processedHash = dh;
			//i = sn->m_b - 1;
			continue; 
		}

		// update values in case we call subloop
		start = i;

		// skip over that delimeter at word #i
		i++;

		// if we had back-to-back br tags make i point to word
		// after the last br tag
		if ( delimeter == (char *)0x01 ) i = delimEnd;

		// find the next <hr> tag, if any, stop at end of "sn"
		for ( ; i < m_nw ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop at end of section "sn"
			if ( i >= sn->m_b ) break;
			// get his section
			Section *si = m_sectionPtrs[i];
			// delimeters that start their own sections must
			// grow out to their parent
			//if ( delimIsSection )
			//	si = si->m_parent;
			// ignore if not the right parent
			if ( si != sn ) continue; 
			// a quicky
			if ( isDelimeter ( i , delimeter , &delimEnd ) ) break;
		}

		// now add the <hr> section above word #i
		goto subloop;
	}
	return true;
}

// this is a function because we also call it from addImpliedSections()!
void Sections::setNextBrotherPtrs ( bool setContainer ) {

	// clear out
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );

		si->m_nextBrother = NULL;
		si->m_prevBrother = NULL;
	}


	//for ( int32_t i = 0 ; i + 1 < m_numSections ; i++ ) {
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );

		Section *sj = NULL;

		// get word after us
		int32_t wn = si->m_b;
		int32_t nw2 = m_nw;

		// if we hit a word in our parent.. then increment wn
		// PROBLEM "<root><t1>hey</t1> blah blah blah x 1 mill</root>"
		// would exhaust the full word list when si is the "t1"
		// section. 
		Section *j2 = si->m_next;
		if ( j2 && j2->m_a >= si->m_b ) {
			sj = j2;
			nw2 = 0;
		}

		// try one more ahead for things like so we don't end up
		// setting sj to the "t2" section as in:
		// "<root><t1><t2>hey</t2></t1> ...."
		if ( ! sj && j2 ) {
			// try the next section then
			j2 = j2->m_next;
			// set "sj" if its a potential brother section
			if ( j2 && j2->m_a >= si->m_b ) {
				sj = j2;
				nw2 = 0;
			}
		}

		// ok, try the next word algo approach
		for ( ; wn < nw2 ; wn++ ) {
			QUICKPOLL(m_niceness);
			sj = m_sectionPtrs[wn];
			if ( sj->m_a >= si->m_b ) break;
		}
		// bail if none
		if ( wn >= m_nw ) continue;

		// telescope up until brother if possible
		for ( ; sj ; sj = sj->m_parent )
			if ( sj->m_parent == si->m_parent ) break;

		// give up?
		if ( ! sj || sj->m_parent != si->m_parent ) continue;

		// sanity check
		if ( sj->m_a < si->m_b && 
		     sj->m_tagId != TAG_TC &&
		     si->m_tagId != TAG_TC ) {
			char *xx=NULL;*xx=0; }
		// set brother
		si->m_nextBrother = sj;
		// set his prev then
		sj->m_prevBrother = si;
		// sanity check
		if ( sj->m_parent != si->m_parent ) { char *xx=NULL;*xx=0; }
		// sanity check
		if ( sj->m_a < si->m_b &&
		     sj->m_tagId != TAG_TC &&
		     si->m_tagId != TAG_TC ) { 
			char *xx=NULL;*xx=0; }
		// do more?
		if ( ! setContainer ) continue;
		// telescope this
		Section *te = sj;
		// telescope up until it contains "si"
		for ( ; te && te->m_a > si->m_a ; te = te->m_parent );
		// only update list container if smaller than previous
		if ( ! si->m_listContainer )
			si->m_listContainer = te;
		else if ( te->m_a > si->m_listContainer->m_a )
			si->m_listContainer = te;
		if ( ! sj->m_listContainer )
			sj->m_listContainer = te;
		else if ( te->m_a > sj->m_listContainer->m_a )
			sj->m_listContainer = te;

		// now 
	}
}


void Sections::setNextSentPtrs ( ) {

	// kinda like m_rootSection
	m_firstSent = NULL;
	m_lastSent  = NULL;

	Section *finalSec = NULL;
	Section *lastSent = NULL;
	// scan the sentence sections and number them to set m_sentNum
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// record final section
		finalSec = sk;
		// set this
		sk->m_prevSent = lastSent;
		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
		// first one?
		if ( ! m_firstSent ) m_firstSent = sk;
		// we are the sentence now
		lastSent = sk;
	}
	// update
	m_lastSent = lastSent;
	// reset this
	lastSent = NULL;
	// now set "m_nextSent" of each section
	for ( Section *sk = finalSec ; sk ; sk = sk->m_prev ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set this
		sk->m_nextSent = lastSent;
		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
		// we are the sentence now
		lastSent = sk;
	}
}

#define TABLE_ROWS 25

void Sections::printFlags (SafeBuf *sbuf , Section *sn ) {
	sec_t f = sn->m_flags;

	if ( f & SEC_HASHXPATH )
		sbuf->safePrintf("hashxpath ");

	sbuf->safePrintf("indsenthash64=%"UINT64" ",sn->m_indirectSentHash64);

	if ( f & SEC_HR_CONTAINER )
		sbuf->safePrintf("hrcontainer ");

	if ( f & SEC_HEADING_CONTAINER )
		sbuf->safePrintf("headingcontainer ");
	if ( f & SEC_HEADING )
		sbuf->safePrintf("heading ");

	if ( f & SEC_TAIL_CRAP )
		sbuf->safePrintf("tailcrap ");

	if ( f & SEC_STRIKE )
		sbuf->safePrintf("strike ");
	if ( f & SEC_STRIKE2 )
		sbuf->safePrintf("strike2 ");

	if ( f & SEC_SECOND_TITLE )
		sbuf->safePrintf("secondtitle ");

	if ( f & SEC_TABLE_HEADER )
		sbuf->safePrintf("tableheader ");

	if ( f & SEC_HAS_DOM )
		sbuf->safePrintf("hasdom " );
	if ( f & SEC_HAS_MONTH )
		sbuf->safePrintf("hasmonth " );
	if ( f & SEC_HAS_DOW )
		sbuf->safePrintf("hasdow " );
	if ( f & SEC_HAS_TOD )
		sbuf->safePrintf("hastod " );
	if ( f & SEC_HASEVENTDOMDOW )
		sbuf->safePrintf("haseventdomdow " );

	if ( f & SEC_MENU_SENTENCE )
		sbuf->safePrintf("menusentence " );
	if ( f & SEC_MENU )
		sbuf->safePrintf("ismenu " );
	if ( f & SEC_MENU_HEADER )
		sbuf->safePrintf("menuheader " );

	if ( f & SEC_CONTAINER )
		sbuf->safePrintf("listcontainer " );
	if ( f & SEC_INPUT_HEADER )
		sbuf->safePrintf("inputheader " );
	if ( f & SEC_INPUT_FOOTER )
		sbuf->safePrintf("inputfooter " );
	if ( f & SEC_LINK_TEXT )
		sbuf->safePrintf("linktext " );
	if ( f & SEC_PLAIN_TEXT )
		sbuf->safePrintf("plaintext " );

	if ( f & SEC_FAKE ) {
		if ( sn->m_baseHash == BH_BULLET )
			sbuf->safePrintf("bulletdelim ");
		else if ( sn->m_baseHash == BH_SENTENCE )
			sbuf->safePrintf("<b>sentence</b> ");
		else if ( sn->m_baseHash == BH_IMPLIED )
			sbuf->safePrintf("<b>impliedsec</b> ");
		else { char *xx=NULL;*xx=0; }
	}

	if ( f & SEC_SPLIT_SENT )
		sbuf->safePrintf("<b>splitsent</b> ");

	if ( f & SEC_NOTEXT )
		sbuf->safePrintf("notext ");

	if ( f & SEC_MULTIDIMS )
		sbuf->safePrintf("multidims ");

	if ( f & SEC_HASDATEHEADERROW )
		sbuf->safePrintf("hasdateheaderrow ");
	if ( f & SEC_HASDATEHEADERCOL )
		sbuf->safePrintf("hasdateheadercol ");



	if ( sn->m_colNum ) 
		sbuf->safePrintf("colnum=%"INT32" ",sn->m_colNum );
	if ( sn->m_rowNum ) 
		sbuf->safePrintf("rownum=%"INT32" ",sn->m_rowNum );
	if ( sn->m_headColSection )
		sbuf->safePrintf("headcola=%"INT32" ",sn->m_headColSection->m_a);
	if ( sn->m_headRowSection )
		sbuf->safePrintf("headrowa=%"INT32" ",sn->m_headRowSection->m_a);

	if ( f & SEC_IN_TABLE )
		sbuf->safePrintf("intable ");
	if ( f & SEC_SCRIPT )
		sbuf->safePrintf("inscript ");
	if ( f & SEC_NOSCRIPT )
		sbuf->safePrintf("innoscript ");
	if ( f & SEC_STYLE )
		sbuf->safePrintf("instyle ");
	if ( f & SEC_HIDDEN )
		sbuf->safePrintf("indivhide ");
	if ( f & SEC_SELECT )
		sbuf->safePrintf("inselect ");
	if ( f & SEC_MARQUEE )
		sbuf->safePrintf("inmarquee ");
	if ( f & SEC_IN_TITLE )
		sbuf->safePrintf("intitle ");
	if ( f & SEC_IN_HEADER )
		sbuf->safePrintf("inheader ");

	if ( f & SEC_UNBALANCED )
		sbuf->safePrintf("unbalanced " );
	if ( f & SEC_OPEN_ENDED )
		sbuf->safePrintf("openended " );

	// sentence flags
	sentflags_t sf = sn->m_sentFlags;
	for ( int32_t i = 0 ; i < 64 ; i++ ) {
		// get mask
		uint64_t mask = ((uint64_t)1) << (uint64_t)i;
		if ( sf & mask )
			sbuf->safePrintf("%s ",getSentBitLabel(mask));
	}

}

char *getSectionTypeAsStr ( int32_t sectionType ) {
	//if ( sectionType == SV_TEXTY             ) return "texty";
	if ( sectionType == SV_CLOCK             ) return "clock";
	if ( sectionType == SV_EURDATEFMT        ) return "eurdatefmt";
	if ( sectionType == SV_EVENT             ) return "event";
	if ( sectionType == SV_ADDRESS           ) return "address";
	if ( sectionType == SV_TAGPAIRHASH       ) return "tagpairhash";
	if ( sectionType == SV_TAGCONTENTHASH    ) return "tagcontenthash";
	if ( sectionType == SV_TURKTAGHASH       ) return "turktaghash";
	//if ( sectionType == SV_DUP               ) return "dup";
	//if ( sectionType == SV_NOT_DUP           ) return "notdup";
	//if ( sectionType == SV_TEXTY_MAX_SAMPLED ) return "textymaxsmpl";
	//if ( sectionType == SV_WAITINLINE        ) return "waitinline";
	if ( sectionType == SV_FUTURE_DATE       ) return "futuredate";
	if ( sectionType == SV_CURRENT_DATE      ) return "currentdate";
	if ( sectionType == SV_PAST_DATE         ) return "pastdate";
	if ( sectionType == SV_SITE_VOTER        ) return "sitevoter";
	// sanity check
	char *xx=NULL;*xx=0;
	return "unknown";
}

bool Sections::isHardSection ( Section *sn ) {
	int32_t a = sn->m_a;
	// . treat this as hard... kinda like a div section...
	//   fixes gwair.org date from stealing address of another date
	//   because the span tags are fucked up...
	// . crap, no this prevents publicbroadcasting.net and other urls
	//   from telescoping to header dates they need to telescope to.
	//   the header dates are in span tags and if that is seen as a hard
	//   section bad things happen
	//if ( m_tids[a] == TAG_SPAN ) return true;
	if ( ! isBreakingTagId(m_tids[a]) ) {
		// . if first child is hard that works!
		// . fixes "<blockquote><p>..." for collectorsguide.com
		if ( sn->m_next && 
		     sn->m_next->m_tagId &&
		     // fix "blah blah<br>blah blah" for sentence
		     sn->m_next->m_tagId != TAG_BR &&
		     sn->m_next->m_a < sn->m_b &&
		     isBreakingTagId(sn->m_next->m_tagId) )
			return true;
		// otherwise, forget it!
		return false;
	}
	// trumba.com has sub dates in br-based implied sections that need
	// to telescope to their parent above
	if ( m_tids[a] == TAG_BR ) return false;
	if ( sn->m_flags & SEC_SENTENCE ) return false;

	// xml tag exception for gwair.org. treat <st1:Place>... as soft
	if ( (m_tids[a] & BACKBITCOMP) == TAG_XMLTAG && ! m_isRSSExt )
		return false;

	return true;
}


bool Sections::setMenus ( ) {

	// . this just returns if already set
	// . sets Bits::m_bits[x].m_flags & D_IN_LINK if its in a link
	// . this bits array is 1-1 with the words
	m_bits->setInLinkBits(this);

	// shortcut
	wbit_t *bb = m_bits->m_bits;

	sec_t flag;
	// set SEC_PLAIN_TEXT and SEC_LINK_TEXT for all sections
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need alnum word
		if ( ! m_wids[i] ) continue;
		// get our flag
		if ( bb[i] & D_IN_LINK ) flag = SEC_LINK_TEXT;
		else                     flag = SEC_PLAIN_TEXT;
		// get section ptr
		Section *sk = m_sectionPtrs[i];
		// loop for sk
		for ( ; sk ; sk = sk->m_parent ) {
			// skip if already set
			if ( sk->m_flags & flag ) break;
			// set it
			sk->m_flags |= flag;
		}
	}

	Section *last = NULL;
	// . alernatively, scan through all anchor tags
	// . compare to last anchor tag
	// . and blow up each to their max non-intersection section and make
	//   sure no PLAIN text in either of those!
	// . this is all to fix texasdrums.drums.org which has various span
	//   and bold tags throughout its menu at random
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . if we hit plain text, we kill our last
		// . this was causing "geeks who drink" for blackbirdbuvette
		//   to get is SEC_MENU set because there was a link after it
		if ( si->m_flags & SEC_PLAIN_TEXT ) last = NULL;
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// . if it is a mailto link forget it
		// . fixes abtango.com from detecting a bad menu
		char *ptr  = m_wptrs[si->m_a];
		int32_t  plen = m_wlens[si->m_a];
		char *mailto = strncasestr(ptr,plen,"mailto:");
		if ( mailto ) last = NULL;
		// bail if no last
		if ( ! last ) { last = si; continue; }
		// save last
		Section *prev = last;
		// set last for next round, used "saved" below
		last = si;
		// get first "hard" section encountered while telescoping
		Section *prevHard = NULL;
		// blow up last until right before it contains us
		for ( ; prev ; prev = prev->m_parent ) {
			// record?
			if ( ! prevHard && isHardSection(prev) ) 
				prevHard = prev;
			// if parent contains us, stop
			if ( prev->m_parent->contains ( si ) ) break;
		}
		// if it has plain text, forget it!
		if ( prev->m_flags & SEC_PLAIN_TEXT ) continue;
		// use this for us
		Section *sk = si;
		// get first "hard" section encountered while telescoping
		Section *skHard = NULL;
		// same for us
		for ( ; sk ; sk = sk->m_parent ) {
			// record?
			if ( ! skHard && isHardSection(sk) ) skHard = sk;
			// if parent contains us, stop
			if ( sk->m_parent->contains ( prev ) ) break;
		}
		// if it has plain text, forget it!
		if ( sk->m_flags & SEC_PLAIN_TEXT ) continue;

		// . first hard sections encountered must match!
		// . otherwise for switchborad.com we lose "A B C ..." as
		//   title candidate because we think it is an SEC_MENU
		//   because the sections before it have links in them, but
		//   they have different hard sections
		if (   prevHard && ! skHard ) continue;
		if ( ! prevHard &&   skHard ) continue;
		if ( prevHard && prevHard->m_tagId!=skHard->m_tagId ) continue;

		// ok, great that works!
		prev->m_flags |= SEC_MENU;
		sk  ->m_flags |= SEC_MENU;
	}

	// . set text around input radio checkboxes text boxes and text areas
	// . we need input tags to be their own sections though!
	//for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need a tag
		if ( ! m_tids[i] ) continue;
		// must be an input tag
		if ( m_tids[i] != TAG_INPUT &&
		     m_tids[i] != TAG_TEXTAREA &&
		     m_tids[i] != TAG_SELECT )
			continue;
		// get tag as a word
		char *tag    = m_wptrs[i];
		int32_t  tagLen = m_wlens[i];
		// what type of input tag is this? hidden? radio? checkbox?...
		int32_t  itlen;
		char *it = getFieldValue ( tag , tagLen , "type" , &itlen );
		// skip if hidden
		if ( itlen==6 && !strncasecmp ( it,"hidden",6) ) continue;
		// get word before first item in list
		int32_t r = i - 1;
		for ( ; r >= 0 ; r-- ) {
			QUICKPOLL(m_niceness);
			// skip if not wordid
			if ( ! m_wids[r] ) continue;
			// get its section
			Section *sr = m_sectionPtrs[r];
			// . skip if in div hidden section
			// . fixes www.switchboard.com/albuquerque-nm/doughnuts
			if ( sr->m_flags & SEC_HIDDEN ) continue;
			// ok, stop
			break;
		}
		// if no header, skip
		if ( r < 0 ) continue;
		// we are the first item
		Section *first = m_sectionPtrs[i];
		// set SEC_INPUT_HEADER
		setHeader ( r , first , SEC_INPUT_HEADER );

		// and the footer, if any
		r = i + 1;

		for ( ; r < m_nw && ! m_wids[r] ; r++ ) QUICKPOLL(m_niceness);

		// if no header, skip
		if ( r >= m_nw ) continue;

		// set SEC_INPUT_FOOTER
		setHeader ( r , first , SEC_INPUT_FOOTER );

	}


	int64_t h_copyright = hash64n("copyright");
	// copyright check
	// the copyright symbol in utf8 (see Entities.cpp for the code)
	char copy[3];
	copy[0] = 0xc2;
	copy[1] = 0xa9;
	copy[2] = 0x00;
	// scan all years, lists and ranges of years, and look for
	// a preceeding copyright sign. mark such years as DF_COPYRIGHT
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// skip if tag
		if ( m_tids[i] ) continue;
		// do we have an alnum word before us here?
		if ( m_wids[i] ) {
			// if word check for copyright
			if ( m_wids[i] != h_copyright ) continue;
		}
		// must have copyright sign in it i guess
		else if ( ! gb_strncasestr(m_wptrs[i],m_wlens[i],copy)) 
			continue;
		// mark section as copyright section then
		Section *sp = m_sectionPtrs[i];
		// flag as menu
		sp->m_flags |= SEC_MENU;
	}


	sec_t ff = SEC_MENU | 
		SEC_INPUT_HEADER | 
		SEC_INPUT_FOOTER;

	// set SEC_MENU of child sections of SEC_MENU sections
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be a link text only section
		if ( ! ( si->m_flags & ff ) ) continue;
		// ignore if went down this path
		if ( si->m_used == 82 ) continue;
		// save it
		//Section *parent = si;
		// get first potential kid
		Section *sk = si->m_next;
		// scan child sections
		for ( ; sk ; sk = sk->m_next ) {
			// stop if not contained
			if ( ! si->contains ( sk ) ) break;
			// mark it
			sk->m_flags |= (si->m_flags & ff); // SEC_MENU;
			// ignore in big loop
			sk->m_used = 82;
		}
	}

	//
	// set SEC_MENU_HEADER
	//
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not in a menu
		if ( ! ( sk->m_flags & SEC_MENU ) ) continue;
		// get his list container
		Section *c = sk->m_listContainer;
		// skip if none
		if ( ! c ) continue;
		// already flagged?
		if ( c->m_used == 89 ) continue;
		// do not repeat on any item in this list
		c->m_used = 89;
		// flag all its brothers!
		Section *zz = sk;
		for ( ; zz ; zz = zz->m_nextBrother ) 
			// bail if not in menu
			if ( ! ( zz->m_flags & SEC_MENU ) ) break;
		// if broked it, stop
		if ( zz ) continue;
		//
		// ok, every item in list is a menu item, so try to set header
		//
		// get word before first item in list
		int32_t r = sk->m_a - 1;
		for ( ; r >= 0 && ! m_wids[r] ; r-- )
			QUICKPOLL(m_niceness);
		// if no header, skip
		if ( r < 0 ) continue;
		// set SEC_MENU_HEADER
		setHeader ( r , sk , SEC_MENU_HEADER );
	}

	//
	// set SEC_MENU_SENTENCE flag
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be a link text only section
		if ( ! ( si->m_flags & SEC_MENU ) ) continue;
		// set this
		bool gotSentence = ( si->m_flags & SEC_SENTENCE );
		// set SEC_MENU of the sentence
		if ( gotSentence ) continue;
		// parent up otherwise
		for ( Section *sk = si->m_parent ; sk ; sk = sk->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if sentence finally
			if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
			// not a menu sentence if it has plain text in it
			// though! we have to make this exception to stop
			// stuff like 
			// "Wedding Ceremonies, No preservatives, more... "
			// from switchboard.com from being a menu sentence
			// just because "more" is in a link.
			if ( sk->m_flags & SEC_PLAIN_TEXT ) break;
			// set it
			sk->m_flags |= SEC_MENU_SENTENCE;
			// and stop
			break;
		}
	}

	// . now set generic list headers
	// . list headers can only contain one hard section with text
	// . list headers cannot have a previous brother section
	for ( int32_t i = 0 ; i + 1 < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// shortcut
		Section *si = &m_sections[i];
		// get container
		Section *c = si->m_listContainer;
		// skip if no container
		if ( ! c ) continue;
		// skip if already did this container
		if ( c->m_used == 55 ) continue;
		// mark it
		c->m_used = 55;
		// flag it
		c->m_flags |= SEC_CONTAINER;
		// skip the rest for now
		continue;
	}

	static bool s_init = false;
	static int64_t h_close ;
	static int64_t h_send ;
	static int64_t h_map ;
	static int64_t h_maps ;
	static int64_t h_directions ;
	static int64_t h_driving ;
	static int64_t h_help ;
	static int64_t h_more ;
	static int64_t h_log ;
	static int64_t h_sign ;
	static int64_t h_change ;
	static int64_t h_write ;
	static int64_t h_save ;
	static int64_t h_share ;
	static int64_t h_forgot ;
	static int64_t h_home ;
	static int64_t h_sitemap ;
	static int64_t h_advanced ;
	static int64_t h_go ;
	static int64_t h_website ;
	static int64_t h_view;
	static int64_t h_add;
	static int64_t h_submit;
	static int64_t h_get;
	static int64_t h_about;
	// new stuff
	static int64_t h_back; // back to top
	static int64_t h_next;
	static int64_t h_buy; // buy tickets
	static int64_t h_english; // english french german versions
	static int64_t h_click;

	if ( ! s_init ) {
		s_init = true;
		h_close = hash64n("close");
		h_send = hash64n("send");
		h_map = hash64n("map");
		h_maps = hash64n("maps");
		h_directions = hash64n("directions");
		h_driving = hash64n("driving");
		h_help = hash64n("help");
		h_more = hash64n("more");
		h_log = hash64n("log");
		h_sign = hash64n("sign");
		h_change = hash64n("change");
		h_write = hash64n("write");
		h_save = hash64n("save");
		h_share = hash64n("share");
		h_forgot = hash64n("forgot");
		h_home = hash64n("home");
		h_sitemap = hash64n("sitemap");
		h_advanced = hash64n("advanced");
		h_go = hash64n("go");
		h_website = hash64n("website");
		h_view = hash64n("view");
		h_add = hash64n("add");
		h_submit = hash64n("submit");
		h_get = hash64n("get");
		h_about = hash64n("about");
		h_back = hash64n ("back");
		h_next = hash64n ("next");
		h_buy = hash64n ("buy");
		h_english = hash64n ("english");
		h_click = hash64n ("click");
	}

	// . when dup/non-dup voting info is not available because we are
	//   more or less an isolated page, guess that these links are
	//   menu links and not to be considered for title or event description
	// . we completely exclude a word from title/description if its 
	//   SEC_MENU is set.
	// . set SEC_MENU for renegade links that start with an action
	//   verb like "close" or "add" etc. but if their # of non dup votes
	//   is high relative to their # of dup votes, then do not set this
	//   because it might be a name of a band like "More" or something
	//   and be in a link
	// . scan all href sections
	// set SEC_LINK_ONLY on sections that just contain a link
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// set points to scan
		int32_t a = si->m_a;
		int32_t b = si->m_b;
		// assume not bad
		bool bad = false;
		int32_t i;
		// scan words if any
		for ( i = a ; i < b ; i++ ) {
			// skip if not word
			if ( ! m_wids[i] ) continue;
			// assume bad
			bad = true;
			// certain words are indicative of menus
			if ( m_wids[i] == h_close ) break;
			if ( m_wids[i] == h_send ) break;
			if ( m_wids[i] == h_map ) break;
			if ( m_wids[i] == h_maps ) break;
			if ( m_wids[i] == h_directions ) break;
			if ( m_wids[i] == h_driving ) break;
			if ( m_wids[i] == h_help ) break;
			if ( m_wids[i] == h_more ) break;
			if ( m_wids[i] == h_log ) break; // log in
			if ( m_wids[i] == h_sign ) break; // sign up/in
			if ( m_wids[i] == h_change ) break; // change my loc.
			if ( m_wids[i] == h_write ) break; // write a review
			if ( m_wids[i] == h_save ) break;
			if ( m_wids[i] == h_share ) break;
			if ( m_wids[i] == h_forgot ) break; // forgot your pwd
			if ( m_wids[i] == h_home ) break;
			if ( m_wids[i] == h_sitemap ) break;
			if ( m_wids[i] == h_advanced ) break; // adv search
			if ( m_wids[i] == h_go ) break; // go to top of page
			if ( m_wids[i] == h_website ) break;
			if ( m_wids[i] == h_view ) break;
			if ( m_wids[i] == h_add ) break;
			if ( m_wids[i] == h_submit ) break;
			if ( m_wids[i] == h_get ) break;
			if ( m_wids[i] == h_about ) break;
			if ( m_wids[i] == h_back ) break;
			if ( m_wids[i] == h_next ) break;
			if ( m_wids[i] == h_buy ) break;
			if ( m_wids[i] == h_english ) break;
			if ( m_wids[i] == h_click ) break;
			bad = false;
			break;
		}
		// skip if ok
		if ( ! bad ) continue;
		// get smallest section
		Section *sm = m_sectionPtrs[i];
		// if bad mark it!
		sm->m_flags |= SEC_MENU;
	}			

	return true;
}

// "first" is first item in the list we are getting header for
void Sections::setHeader ( int32_t r , Section *first , sec_t flag ) {
	// get smallest section containing word #r
	Section *sr = m_sectionPtrs[r];
	// save orig
	Section *orig = sr;

	// blow up until just before "first" section
	for ( ; sr ; sr = sr->m_parent ) {
		// forget it if in title tag already!
		if ( sr->m_flags & SEC_IN_TITLE ) return;
		// stop if no parent
		if ( ! sr->m_parent ) continue;
		// parent must not contain first
		if ( sr->m_parent->contains ( first ) ) break;
	}
	// if we failed to contain "first"... what does this mean? i dunno
	// but its dropping core for
	// http://tedserbinski.com/jcalendar/jcalendar.js
	if ( ! sr ) return;
	
	// save that
	Section *biggest = sr;

	// check out prev brother
	Section *prev = biggest->m_prevBrother;

	// if we are in a hard section and capitalized (part of the 
	// SEC_HEADING) requirements, then it should be ok if we have
	// a prev brother of a different tagid.
	// this will fix americantowns.com which has a list of header tags
	// and ul tags intermingled, with menus in the ul tags.
	// should also fix upcoming.yahoo.com which has alternating
	// dd and dt tags for its menus. now that we got rid of
	// addImpliedSections() we have to deal with this here, and it will
	// be more accurate since addImpliedSections() was often wrong.
	if ( prev &&
	     (orig->m_flags & SEC_HEADING) &&
	     prev->m_tagId != biggest->m_tagId )
		prev = NULL;

	// but if prev brother is a blank, we should view that as a delimeter
	// BUT really we should have added those sections in with the new
	// delimeter logic! but let's put this in for now anyway...
	if ( prev && prev->m_firstWordPos < 0 )
		prev = NULL;

	// if the header section has a prev brother, forget it!
	if ( prev ) return;

	// . if we gained extra text, that is a no-no then
	// . these two checks replaced the two commented out ones above
	// . they allow for empty sections preceeding "sr" at any level as
	//   we telescope it up
	if ( biggest->m_firstWordPos != orig->m_firstWordPos ) return;
	if ( biggest->m_lastWordPos  != orig->m_lastWordPos  ) return;

	// . now blow up first until just before it hits biggest as well
	// . this fixes reverbnation on the nextBrother check below
	for ( ; first ; first = first->m_parent ) {
		// stop if parent is NULL
		if ( ! first->m_parent ) break;
		// stop if parent would contain biggest
		if ( first->m_parent->contains ( biggest ) ) break;
	}
	// if after blowing it up "first" contains more than just menu
	// sections, then bail. that really was not a menu header!
	// fixes reverbnation url that thought "That 1 Guy" was a menu header.
	if ( flag == SEC_MENU_HEADER ) {
		Section *fx = first;
		for ( ; fx ; fx = fx->m_next ) {
			// stop when list is over
			if ( fx->m_a >= first->m_b ) break;
			// ignore if no next
			if ( fx->m_flags & SEC_NOTEXT ) continue;
			// thats bad if SEC_MENU not set, it should be for all!
			if ( fx->m_flags & SEC_MENU ) continue;
			// we got these now
			if ( fx->m_flags & SEC_MENU_SENTENCE ) continue;
			// otherwise, bad!
			return;
			//if ( ! ( fx->m_flags & SEC_MENU ) ) return;
		}
	}

	// strange?
	if ( ! sr ) { char *xx=NULL;*xx=0; }
	// scan until outside biggest
	int32_t lastb = biggest->m_b;
	// . make sure sr does not contain any list in it
	// . scan all sections between sr and "saved"
	for ( ; sr ; sr = sr->m_next ) {
		// stop if over
		if ( sr->m_a >= lastb ) break;
		// if we have a brother with same taghash we are
		// part of a list
		if ( sr->m_nextBrother &&
		     sr->m_nextBrother->m_tagHash == sr->m_tagHash &&
		     sr->m_nextBrother != first )
			return;
		if ( sr->m_prevBrother &&
		     sr->m_prevBrother->m_tagHash == sr->m_tagHash &&
		     // for footers
		     sr->m_prevBrother != first )
			return;
	}

	// restart loop
	sr = biggest;
	// ok, not part of a list, flag it
	for ( ; sr ; sr = sr->m_next ) {
		// stop if over
		if ( sr->m_a >= lastb ) break;
		// flag each subsection
		sr->m_flags |= flag; // SEC_MENU_HEADER;
		// mark it
		//if (flag == SEC_LIST_HEADER ) sr->m_headerOfList = firstOrig;
	}
}


// . set SEC_HEADING and SEC_HEADING_CONTAINER bits in Section::m_flags
// . identifies sections that are most likely headings
// . the WHOLE idea of this algo is to take a list of sections that are all 
//   the same tagId/baseHash and differentiate them so we can insert implied 
//   sections with headers. 
// . then addImpliedSections() uses the SEC_HEADING_CONTAINER bit to
//   modify the base hash to distinguish sections that would otherwise all
//   be the same! 
// . should fix salsapower.com and abqtango.com to have proper implied sections
// . it is better to not add any implied sections than to add the wrong ones
//   so be extra strict in our rules here.
bool Sections::setHeadingBit ( ) {

	int32_t headings = 0;
	// scan the sections
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if directly contains no text
		//if ( si->m_flags & SEC_NOTEXT ) continue;
		// SEC_NOTEXT is not set at this point
		int32_t fwp = si->m_firstWordPos;
		if ( fwp == -1 ) continue;
		// we must be the smallest container around this text
		if ( m_sectionPtrs[fwp] != si ) continue;

		// . make sure we are in our own hard section
		// . TODO: allow for bold or strong, etc. tags as well
		bool hasHard = false;
		int32_t a = si->m_firstWordPos;
		int32_t b = si->m_lastWordPos;
		// go to parent
		Section *pp = si;
		Section *biggest = NULL;
		bool inLink = false;
		// . we need to be isolated in our own hard section container
		// . TODO: what about "<b>Hi There <i>Bob</i></b>" as a heading
		// . i guess that will still work!
		for ( ; pp ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if breached
			if ( pp->m_firstWordPos != a ) break;
			if ( pp->m_lastWordPos  != b ) break;
			// record this
			if ( pp->m_tagId == TAG_A ) inLink = true;
			// record the biggest section containing just our text
			biggest = pp;
			// is it a hard section?
			if ( isHardSection(pp) )  hasHard = true;
			// . allow bold and strong tags
			// . fixes gwair.org which has the dates of the
			//   month in strong tags. so we need to set 
			//   SEC_HEADING for those so getDelimHash() will
			//   recognize such tags as date header tags in the
			//   METHOD_DOM algorithm and we get the proper
			//   implied sections
			if ( pp->m_tagId == TAG_STRONG ) hasHard = true;
			if ( pp->m_tagId == TAG_B      ) hasHard = true;
		}
		// need to be isolated in a hard section
		if ( ! hasHard ) continue;

		// now make sure the text is capitalized etc
		bool hadUpper = false;
		//bool hadLower = false;
		int32_t lowerCount = 0;
		bool hadYear  = false;
		bool hadAlpha = false;
		int32_t i;
		// scan the alnum words we contain
		for ( i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// . did we hit a breaking tag?
			// . "<div> blah <table><tr><td>blah... </div>"
			if ( m_tids[i] && isBreakingTagId(m_tids[i]) ) break;
			// skip if not alnum word
			if ( ! m_wids[i] ) continue;
			// skip digits
			if ( is_digit(m_wptrs[i][0]) ) {
				// skip if not 4-digit year
				if ( m_wlens[i] != 4 ) continue;
				// . but if we had a year like "2010" that
				//   is allowed to be a header.
				// . this fixes 770kob.com because the events
				//   under the "2010" header were telescoping
				//   up into events in the "December 2009"
				//   section, when they should have been in
				//   their own section! and now they are in
				//   their own implied section...
				int32_t num = m_words->getAsLong(i);
				if ( num < 1800 ) continue;
				if ( num > 2100 ) continue;
				// mark it
				hadYear = true;
				continue;
			}
			// mark this
			hadAlpha = true;
			// is it upper?
			if ( is_upper_utf8(m_wptrs[i]) ) {
				hadUpper = true;
				continue;
			}
			// skip stop words
			if ( m_words->isStopWord(i) ) continue;
			// . skip short words
			// . November 4<sup>th</sup> for facebook.com
			if ( m_wlens[i] <= 2 ) continue;
			// is it lower?
			if ( is_lower_utf8(m_wptrs[i]) ) lowerCount++;
			// stop now if bad
			//if ( hadUpper ) break;
			if ( lowerCount >= 2 ) break;
		}
		// is it a header?
		bool isHeader = hadUpper;
		// a single year by itself is ok though too
		if ( hadYear && ! hadAlpha ) isHeader = true;
		// allow for one mistake like we do in Events.cpp for titles
		if ( lowerCount >= 2 ) isHeader = false;
		if ( ! isHeader ) continue;

		// ok, mark this section as a heading section
		si->m_flags |= SEC_HEADING;

		// mark all parents to up to biggest
		biggest->m_flags |= SEC_HEADING_CONTAINER;

		// mark all up to biggest now too! that way the td section
		// gets marked and if the tr section gets replaced with a 
		// fake tc section then we are ok for METHOD_ABOVE_DOW!
		for ( Section *pp = si; // ->m_parent ;  (bug!)
		      pp && pp != biggest ; 
		      pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			pp->m_flags |= SEC_HEADING_CONTAINER;
		}

		// a hack!
		if ( inLink ) biggest->m_flags |= SEC_LINK_TEXT;

		// count them
		headings++;
	}

	// bail now if no headings were set
	if ( ! headings ) return true;

	return true;
}

void Sections::setTagHashes ( ) {

	if ( m_numSections == 0 ) return;

	// now recompute the tagHashes and depths and content hashes since
	// we have eliminate open-ended sections in the loop above
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// these have to be in order of sn->m_a to work right
		// because we rely on the parent tag hash, which would not
		// necessarily be set if we were not sorted, because the 
		// parent section could have SEC_FAKE flag set because it is
		// a br section added afterwards.
		//Section *sn = m_sorted[i]; // sections[i];
		// shortcut
		int64_t bh = (int64_t)sn->m_baseHash;
		//int64_t fh = sn->m_tagId;
		// sanity check
		if ( bh == 0 ) { char *xx=NULL;*xx=0; }
		// if no parent, use initial values
		if ( ! sn->m_parent ) {
			sn->m_depth   = 0;
			sn->m_tagHash = bh;
			sn->m_turkTagHash32 = sn->m_turkBaseHash;//m_tagId;
			//sn->m_turkTagHash32 = bh;
			//sn->m_formatHash = fh;
			// sanity check
			if ( bh == 0 ) { char *xx=NULL;*xx=0; }
			continue;
		}
		// sanity check
		if ( sn->m_parent->m_tagHash == 0 ) { char *xx=NULL;*xx=0; }

		// . update the cumulative front tag hash
		// . do not include hyperlinks as part of the cumulative hash!
		sn->m_tagHash = hash32h ( bh , sn->m_parent->m_tagHash );

		// now use this for setting Date::m_dateTagHash instead
		// of using Section::m_tagHash since often the dates like
		// for zvents.org are in a <tr id=xxxx> where xxxx changes
		sn->m_turkTagHash32 = 
			//hash32h ( sn->m_tagId, sn->m_parent->m_turkTagHash );
			hash32h ( sn->m_turkBaseHash,
				  sn->m_parent->m_turkTagHash32 );

		sn->m_colorHash = hash32h ( bh , sn->m_parent->m_colorHash );

		// if we are an implied section, just use the tag hash of
		// our parent. that way since we add different implied
		// sections for msichicago.com root than we do the kid,
		// the section voting should still match up
		if ( bh == BH_IMPLIED ) {
			sn->m_tagHash     = sn->m_parent->m_tagHash;
			sn->m_turkTagHash32 = sn->m_parent->m_turkTagHash32;
		}

		// sanity check
		// i've seen this happen once for
		// sn->m_parent->m_tagHash = 791013962
		// bh = 20020
		if ( sn->m_tagHash == 0 ) sn->m_tagHash = 1234567;
		// depth based on parent, too
		//if ( tid != TAG_A ) sn->m_depth = sn->m_parent->m_depth + 1;
		//else                sn->m_depth = sn->m_parent->m_depth    ;
		sn->m_depth = sn->m_parent->m_depth + 1;
	}
}

bool Sections::containsTagId ( Section *si, nodeid_t tagId ) {
	// scan sections to right
	int32_t a = si->m_a + 1;
	// scan as int32_t as contained by us
	for ( ; a < m_nw && a < si->m_b ; a++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( m_tids[a] == tagId ) return true;
	}
	return false;
}

// . just the voting info for passing into diffbot in json
// . along w/ the title/summary/etc. we can return this json blob for each search result
bool Sections::printVotingInfoInJSON ( SafeBuf *sb ) {

	// save ptrs
	m_sbuf = sb;
	m_sbuf->setLabel ("sectprnt2");

	// print sections out
	for ( Section *sk = m_rootSection ; sk ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// print this section
		printSectionDiv ( sk , FORMAT_JSON ); // forProCog );
		// advance
		int32_t b = sk->m_b;
		// stop if last
		if ( b >= m_nw ) break;
		// get section after that
		sk = m_sectionPtrs[b];
	}

	// ensure ends in \0
	if ( ! sb->nullTerm() ) return false;

	return true;
}


// make this replace ::print() when it works
bool Sections::print2 ( SafeBuf *sbuf ,
			int32_t hiPos,
			int32_t *wposVec,
			char *densityVec,
			char *diversityVec,
			char *wordSpamVec,
			char *fragVec,
			char format ) {
	//FORMAT_PROCOG FORMAT_JSON HTML

	//sbuf->safePrintf("<b>Sections in Document</b>\n");

	// save ptrs
	m_sbuf = sbuf;

	m_sbuf->setLabel ("sectprnt");

	m_hiPos = hiPos;

	m_wposVec      = wposVec;
	m_densityVec   = densityVec;
	m_diversityVec = diversityVec;
	m_wordSpamVec  = wordSpamVec;
	m_fragVec      = fragVec;

	//verifySections();

	char  **wptrs = m_words->getWords    ();
	int32_t   *wlens = m_words->getWordLens ();
	//nodeid_t *tids = m_words->getTagIds();
	int32_t    nw    = m_words->getNumWords ();

	// check words
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		Section *sn = m_sectionPtrs[i];
		if ( sn->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b <= i ) { char *xx=NULL;*xx=0; }
	}


	// print sections out
	for ( Section *sk = m_rootSection ; sk ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// print this section
		printSectionDiv ( sk , format );//forProCog );
		// advance
		int32_t b = sk->m_b;
		// stop if last
		if ( b >= m_nw ) break;
		// get section after that
		sk = m_sectionPtrs[b];
	}

	if ( format != FORMAT_HTML ) return true; // forProCog

	// print header
	char *hdr =
		"<table border=1>"
		"<tr>"
		"<td><b>sec #</b></td>"
		"<td><b>wordStart</b></td>"
		"<td><b>wordEnd</b></td>"
		"<td><b>baseHash</b></td>"
		"<td><b>cumulTagHash</b></td>"
		"<td><b>contentHash</b></td>"
		"<td><b>contentTagHash</b></td>"
		"<td><b>XOR</b></td>" // only valid for contentHashes
		"<td><b>alnum words</b></td>" // contained in section
		"<td><b>depth</b></td>"
		"<td><b>parent word range</b></td>"
		"<td><b># siblings</b></td>"
		"<td><b>flags</b></td>"
		"<td><b>evIds</b></td>"
		"<td><b>text snippet</b></td>"
		//"<td>votes for static</td>"
		//"<td>votes for dynamic</td>"
		//"<td>votes for texty</td>"
		//"<td>votes for unique</td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr);

	int32_t rcount = 0;
	int32_t scount = 0;
	// show word # of each section so we can look in PageParser.cpp's
	// output to see exactly where it starts, since we now label all
	// the words
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr);
		// get it
		//Section *sn = &m_sections[i];
		//Section *sn = m_sorted[i];
		// skip if not a section with its own words
		//if ( sn->m_flags & SEC_NOTEXT ) continue;
		char *xs = "--";
		char ttt[100];
		if ( sn->m_contentHash64 ) {
			int32_t modified = sn->m_tagHash ^ sn->m_contentHash64;
			sprintf(ttt,"0x%"XINT32"",modified);
			xs = ttt;
		}
		// shortcut
		Section *parent = sn->m_parent;
		int32_t pswn = -1;
		int32_t pewn = -1;
		if ( parent ) pswn = parent->m_a;
		if ( parent ) pewn = parent->m_b;
		// print it
		sbuf->safePrintf("<!--ignore--><tr><td>%"INT32"</td>\n"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>%"INT32" to %"INT32"</nobr></td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>" ,
				 scount++,//i,
				 sn->m_a,
				 sn->m_b,
				 (int32_t)sn->m_baseHash,
				 (int32_t)sn->m_tagHash,
				 (int32_t)sn->m_contentHash64,
				 (int32_t)(sn->m_contentHash64^sn->m_tagHash),
				 xs,
				 sn->m_exclusive,
				 sn->m_depth,
				 pswn,
				 pewn,
				 sn->m_numOccurences);//totalOccurences );
		// now show the flags
		printFlags ( sbuf , sn );
		// first few words of section
		int32_t a = sn->m_a;
		int32_t b = sn->m_b;
		// -1 means an unclosed tag!! should no longer be the case
		if ( b == -1 ) { char *xx=NULL;*xx=0; }//b=m_words->m_numWords;
		sbuf->safePrintf("</nobr></td>");

		sbuf->safePrintf("<td>&nbsp;</td>");

		sbuf->safePrintf("<td><nobr>");
		// 70 chars max
		int32_t   max   = 70; 
		int32_t   count = 0;
		char   truncated = 0;
		// do not print last word/tag in section
		for ( int32_t i = a ; i < b - 1 && count < max ; i++ ) {
			char *s    = wptrs[i];
			int32_t  slen = wlens[i];
			if ( count + slen > max ) {
				truncated = 1; 
				slen = max - count;
			}
			count += slen;
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("<b>");
			sbuf->htmlEncode(s,slen,false);
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("</b>");
		}
		// if we truncated print a ...
		if ( truncated ) sbuf->safePrintf("<b>...</b>");
		// then print ending tag
		if ( b < nw ) {
			int32_t blen = wlens[b-1];
			if ( blen>20 ) blen = 20;
			sbuf->safePrintf("<b>");
			sbuf->htmlEncode(wptrs[b-1],blen,false);
			sbuf->safePrintf("</b>");
		}

		sbuf->safePrintf("</nobr></td></tr>\n");
	}
			 
	sbuf->safePrintf("</table>\n<br>\n");


	return true;
}

bool Sections::printSectionDiv ( Section *sk , char format ) {
	//log("sk=%"INT32"",sk->m_a);
	// enter a new div section now
	m_sbuf->safePrintf("<br>");
	// only make font color different
	int32_t bcolor = (int32_t)sk->m_colorHash& 0x00ffffff;
	int32_t fcolor = 0x000000;
	int32_t rcolor = 0x000000;
	uint8_t *bp = (uint8_t *)&bcolor;
	bool dark = false;
	if ( bp[0]<128 && bp[1]<128 && bp[2]<128 ) 
		dark = true;
	// or if two are less than 50
	if ( bp[0]<100 && bp[1]<100 ) dark = true;
	if ( bp[1]<100 && bp[2]<100 ) dark = true;
	if ( bp[0]<100 && bp[2]<100 ) dark = true;
	// if bg color is dark, make font color light
	if ( dark ) {
		fcolor = 0x00ffffff;
		rcolor = 0x00ffffff;
	}
	// start the new div
	m_sbuf->safePrintf("<div "
			 "style=\""
			 "background-color:#%06"XINT32";"
			 "margin-left:20px;"
			 "border:#%06"XINT32" 1px solid;"
			 "color:#%06"XINT32"\">",
			 //(int32_t)sk,
			 bcolor,
			 rcolor,
			 fcolor);

	bool printWord = true;
	if ( ! sk->m_parent && sk->m_next && sk->m_next->m_a == sk->m_a )
		printWord = false;

	// print word/tag #i
	if ( !(sk->m_flags&SEC_FAKE) && sk->m_tagId && printWord )
		// only encode if it is a tag
		m_sbuf->htmlEncode(m_wptrs[sk->m_a],m_wlens[sk->m_a],false );

	m_sbuf->safePrintf("<i>");

	// print the flags
	m_sbuf->safePrintf("A=%"INT32" ",sk->m_a);

	// print tag hash now
	m_sbuf->safePrintf("taghash=%"UINT32" ",(int32_t)sk->m_tagHash);

	m_sbuf->safePrintf("turktaghash=%"UINT32" ",
	                   (int32_t)sk->m_turkTagHash32);

	if ( sk->m_contentHash64 )
		m_sbuf->safePrintf("ch64=%"UINT64" ",sk->m_contentHash64);
	if ( sk->m_sentenceContentHash64 &&
	     sk->m_sentenceContentHash64 != sk->m_contentHash64 )
		m_sbuf->safePrintf("sch=%"UINT64" ",
		                   sk->m_sentenceContentHash64);


	// show this stuff for tags that contain sentences indirectly,
	// that is what we hash in XmlDoc::hashSections()
	//if(sk->m_indirectSentHash64 && sk->m_tagId != TAG_TEXTNODE) {
	uint64_t mod = 0;
	if ( sk->m_flags & SEC_HASHXPATH ) {
		// show for all tags now because diffbot wants to see
		// markup on all tags
		//if ( sk->m_indirectSentHash64 && sk->m_tagId !=TAG_TEXTNODE){
		//if ( sk->m_stats.m_totalDocIds ) {
		mod = (uint32_t)sk->m_turkTagHash32;
		mod ^= (uint32_t)(uint64_t)m_siteHash64;
		m_sbuf->safePrintf("<a style=decoration:none; "
		                   "href=/search?c=%s&"
		                   "q=gbfacetstr%%3A"
		                   "gbxpathsitehash%"UINT64">"
		                   //"<u>"
		                   "xpathsitehash=%"UINT64""
		                   //"</u>"
		                   "</a> "
		                   //"</font> "
		                   ,m_coll
		                   ,mod
		                   ,mod);
	}

	SectionStats *ss = &sk->m_stats;

	// also the value of the inner html hashed
	if ( sk->m_flags & SEC_HASHXPATH ) {//ss->m_totalMatches > 0) {
		uint32_t val ;
		val = (uint32_t) sk->m_indirectSentHash64 ;
		m_sbuf->safePrintf("xpathsitehashval=%"UINT32" ", val );
	}

	// some voting stats
	if ( sk->m_flags & SEC_HASHXPATH ) {//ss->m_totalMatches > 0) {
		m_sbuf->safePrintf("_s=M%"INT32"D%"INT32"n%"INT32"u%"INT32"h%"UINT32" "
		                   ,(int32_t)ss->m_totalMatches
		                   ,(int32_t)ss->m_totalDocIds
		                   ,(int32_t)ss->m_totalEntries
		                   ,(int32_t)ss->m_numUniqueVals
		                   ,(uint32_t)mod
		                   );
	}

	if ( sk->m_lastLinkContentHash32 )
		m_sbuf->safePrintf("llch=%"UINT32" ",
		                   (int32_t)sk->m_lastLinkContentHash32);

	if ( sk->m_leftCell )
		m_sbuf->safePrintf("leftcellA=%"INT32" ",
		                   (int32_t)sk->m_leftCell->m_a);
	if ( sk->m_aboveCell )
		m_sbuf->safePrintf("abovecellA=%"INT32" ",
		                   (int32_t)sk->m_aboveCell->m_a);

	printFlags ( m_sbuf , sk );
	
	if ( isHardSection(sk) )
		m_sbuf->safePrintf("hardsec ");
	
	m_sbuf->safePrintf("</i>\n");

	// now print each word and subsections in this section
	int32_t a = sk->m_a;
	int32_t b = sk->m_b;
	for ( int32_t i = a ; i < b ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// . if its a and us, skip
		// . BUT if we are root then really this tag belongs to
		//   our first child, so make an exception for root!
		if ( i == a && m_tids[i] && (sk->m_parent) ) continue;

		// . get section of this word
		// . TODO: what if this was the tr tag we removed??? i guess
		//   maybe make it NULL now?
		Section *ws = m_sectionPtrs[i];
		// get top most parent that starts at word position #a and
		// is not "sk"
		for ( ; ; ws = ws->m_parent ) {
			if ( ws == sk ) break;
			if ( ! ws->m_parent ) break;
			if ( ws->m_parent->m_a != ws->m_a ) break;
			if ( ws->m_parent == sk ) break;
		}
		// if it belongs to another sections, print that section
		if ( ws != sk ) {
			// print out this subsection
			printSectionDiv ( ws , format ); // forProCog );
			// advance to end of that then
			i = ws->m_b - 1;
			// and try next word
			continue;
		}

		// ignore if in style section, etc. just print it out
		if ( sk->m_flags & NOINDEXFLAGS ) {
			m_sbuf->htmlEncode(m_wptrs[i],m_wlens[i],false );
			continue;
		}

		// boldify alnum words
		if ( m_wids[i] ) {
			if ( m_wposVec[i] == m_hiPos )
				m_sbuf->safePrintf("<a name=hipos></a>");
			m_sbuf->safePrintf("<nobr><b>");
			if ( i <  MAXFRAGWORDS && m_fragVec[i] == 0 ) 
				m_sbuf->safePrintf("<strike>");
		}
		if ( m_wids[i] && m_wposVec[i] == m_hiPos )
			m_sbuf->safePrintf("<blink style=\""
					   "background-color:yellow;"
					   "color:black;\">");
		// print that word
		m_sbuf->htmlEncode(m_wptrs[i],m_wlens[i],false );
		if ( m_wids[i] && m_wposVec[i] == m_hiPos )
			m_sbuf->safePrintf("</blink>");
		// boldify alnum words
		if ( m_wids[i] ) {
			if ( i < MAXFRAGWORDS && m_fragVec[i] == 0 ) 
				m_sbuf->safePrintf("</strike>");
			m_sbuf->safePrintf("</b>");
		}
		// and print out their pos/div/spam sub
		if ( m_wids[i] ) {
			m_sbuf->safePrintf("<sub "
					   "style=\"background-color:white;"
					   "font-size:10px;"
					   "border:black 1px solid;"
					   "color:black;\">");
			m_sbuf->safePrintf("%"INT32"",m_wposVec[i]);
			if ( m_densityVec[i] != MAXDENSITYRANK )
				m_sbuf->safePrintf("/<font color=purple><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_densityVec[i]);
			/*
			if ( m_diversityVec[i] != MAXDIVERSITYRANK )
				m_sbuf->safePrintf("/<font color=green><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_diversityVec[i]);
			*/
			if ( m_wordSpamVec[i] != MAXWORDSPAMRANK )
				m_sbuf->safePrintf("/<font color=red><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_wordSpamVec[i]);
			m_sbuf->safePrintf("</sub></nobr>");
		}
	}
	m_sbuf->safePrintf("</div>\n");

	return true;
}

bool Sections::verifySections ( ) {

	// make sure we map each word to a section that contains it at least
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		Section *si = m_sectionPtrs[i];
		if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
		// must have checksum
		if ( m_wids[i] && si->m_contentHash64==0){char *xx=NULL;*xx=0;}
		// must have this set if 0
		if ( ! si->m_contentHash64 && !(si->m_flags & SEC_NOTEXT)) {
			char *xx=NULL;*xx=0;}
		if (   si->m_contentHash64 &&  (si->m_flags & SEC_NOTEXT)) {
			char *xx=NULL;*xx=0;}
	}

	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) 
		// breathe
		QUICKPOLL ( m_niceness );

	// sanity check
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		//Section *sn = &m_sections[i];
		// get parent
		Section *sp = sn->m_parent;
	subloop3:
		// skip if no parent
		if ( ! sp ) continue;
		// make sure parent fully contains
		if ( sp->m_a > sn->m_a ) { char *xx=NULL;*xx=0; }
		if ( sp->m_b < sn->m_b ) { char *xx=NULL;*xx=0; }
		// breathe
		QUICKPOLL ( m_niceness );
		// and make sure every grandparent fully contains us too!
		sp = sp->m_parent;
		goto subloop3;
	}

	// sanity check
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		Section *sn = &m_sections[i];
		if ( sn->m_a >= sn->m_b ) { char *xx=NULL;*xx=0; }
	}

	// sanity check, make sure each section is contained by the
	// smallest section containing it
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		//Section *si = &m_sections[i];
		//if ( ! si ) continue;
		//for ( int32_t j = 0 ; j < m_numSections ; j++ ) {
		for ( Section *sj = m_rootSection ; sj ; sj = sj->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if us
			if ( sj == si ) continue;
			// skip column sections because they are artificial
			// and only truly contain some of the sections that
			// their [a,b) interval says they contain.
			if ( sj->m_tagId == TAG_TC ) continue;
			// or if an implied section of td tags in a tc
			if ( sj->m_baseHash == BH_IMPLIED &&
			     sj->m_parent &&
			     sj->m_parent->m_tagId == TAG_TC ) 
				continue;
			// get him
			//Section *sj = &m_sections[j];
			// skip if sj does not contain first word in si
			if ( sj->m_a >  si->m_a ) continue;
			if ( sj->m_b <= si->m_a ) continue;
			// ok, make sure in our parent path
			Section *ps = si;
			for ( ; ps ; ps = ps->m_parent ) 
				if ( ps == sj ) break;
			// ok if we found it
			if ( ps ) continue;
			// sometimes if sections are equal then the other
			// is the parent
			ps = sj;
			for ( ; ps ; ps = ps->m_parent ) 
				if ( ps == si ) break;
			// must have had us
			if ( ps ) continue;
			char *xx=NULL;*xx=0;
		}
	}
	
	// make sure we map each word to a section that contains it at least
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		Section *si = m_sectionPtrs[i];
		if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
	}

	return true;
}

bool Sections::isTagDelimeter ( class Section *si , nodeid_t tagId ) {
	// store
	Section *saved = si;
	// . people embed tags in other tags, so scan
	// . fix "<strong><em><br></em><strong><span><br></span> for
	//   guysndollsllc.com homepage
	for ( ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if si not contained in saved
		if ( ! saved->contains ( si ) ) return false;
		// stop if got it
		if ( tagId != TAG_BR &&  si->m_tagId == tagId ) break;
		// tag br can be a pr or p tag (any vert whitespace really)
		if ( tagId == TAG_BR ) {
			if ( si->m_tagId == TAG_BR ) break;
			if ( si->m_tagId == TAG_P  ) break;
			// treat <hN> and </hN> as single breaking tags too
			nodeid_t ft = si->m_tagId & BACKBITCOMP;
			if ( ft >= TAG_H1 && ft <= TAG_H5 ) break;
			// fix tennisoncampus.com which has <p>...<table> as
			// a double space
			if ( si->m_tagId == TAG_TABLE  ) break;
		}
		// . skip if has no text of its own
		// . like if looking for header tags and we got:
		//   <tr><td></td><td><h1>stuff here</h1></td></tr>
		//   we do not want the <td></td> to stop us
		//   bluefin-cambridge.com
		if ( si->m_firstWordPos < 0 ) continue;
		// stop if hit alnum before tagid
		//if ( si->m_alnumPosA != saved->m_alnumPosA ) 
		//	return false;
		// stop if hit text before hitting tagid
		if ( si->m_lastWordPos != saved->m_lastWordPos )
			return false;
	}
	// done?
	if ( ! si ) return false; 
	//if ( si->m_tagId != tagId ) return false;
	if ( tagId != TAG_BR ) return true;
	// need double brs (or p tags)
	int32_t a = si->m_a + 1;
	//if ( a + 2 >= m_nw ) return false;
	//if ( m_tids[a+1] == TAG_BR ) return true;
	//if ( m_wids[a+1]           ) return false;
	//if ( m_tids[a+2] == TAG_BR ) return true;
	// guynsdollsllc.com homepage has some crazy br tags
	// in their own em strong tags, etc.
	int32_t kmax = a+10;
	if ( kmax > m_nw ) kmax = m_nw;
	for ( int32_t k = a ; k < kmax ; k++ ) {
		if ( m_wids[k]           ) return false;
		if ( m_tids[k] == TAG_BR ) return true;
		if ( m_tids[k] == TAG_P  ) return true;
		if ( m_tids[k] == TAG_TABLE  ) return true;
		// treat <hN> and </hN> as single breaking tags too
		nodeid_t ft = m_tids[k] & BACKBITCOMP;
		if ( ft >= TAG_H1 && ft <= TAG_H5 ) return true;
	}
	return false;
};

sentflags_t getMixedCaseFlags ( Words *words , 
				wbit_t *bits ,
				int32_t senta , 
				int32_t sentb , 
				int32_t niceness ) {
	
	int64_t *wids = words->getWordIds();
	int32_t *wlens = words->getWordLens();
	char **wptrs = words->getWordPtrs();
	int32_t lowerCount = 0;
	int32_t upperCount = 0;
	bool firstWord = true;
	bool inParens = false;
	for ( int32_t i = senta ; i < sentb ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if not alnum
		if ( ! wids[i] ) {
			// skip tags right away
			if ( wptrs[i][0]=='<' ) continue;
			// check for end first in case of ") ("
			if ( words->hasChar(i,')') ) inParens = false;
			// check if in parens
			if ( words->hasChar(i,'(') ) inParens = true;
			continue;
		}
		// skip if in parens
		if ( inParens ) continue;
		// are we upper case?
		bool upper = is_upper_utf8(wptrs[i]) ;
		// are we a stop word?
		bool isStopWord = words->isStopWord(i);
		// . if first word is stop word and lower case, forget it
		// . fix "by Ron Hutchinson" title for adobetheater.org
		if ( isStopWord && firstWord && ! upper )
			// make sure both flags are returned i guess
			return (SENT_MIXED_CASE | SENT_MIXED_CASE_STRICT);

		// allow if hyphen preceedes like for
		// abqfolkfest.org's "Kay-lee"
		if ( i>0 && wptrs[i][-1]=='-' ) upper = true;
		// if we got mixed case, note that!
		if ( wids[i] &&
		     ! is_digit(wptrs[i][0]) &&
		     ! upper &&
		     (! isStopWord || firstWord ) &&
		     // . November 4<sup>th</sup> for facebook.com
		     // . added "firstword" for "on AmericanTowns.com"
		     //   title prevention for americantowns.com
		     (wlens[i] >= 3 || firstWord) )
			lowerCount++;

		// turn off
		firstWord = false;
		// . don't count words like "Sunday" that are dates!
		// . fixes "6:30 am. Sat. and Sun.only" for unm.edu
		//   and "3:30 pm. - 4 pm. Sat. and Sun., sandwiches"
		// . fixes events.kgoradio.com's
		//   "San Francisco Symphony Chorus sings Bach's 
		//    Christmas Oratorio"
		// . fixes "7:00-8:00pm, Tango Fundamentals lesson" for
		//   abqtango.com
		// . fixes "Song w/ Joanne DelCarpine (located" for
		//   texasdrums.drums.org
		// . "Loren Kahn Puppet and Object Theater presents 
		//    Billy Goat Ball" for trumba.com
		if ( bits[i] & D_IS_IN_DATE ) upper = false;
		// . was it upper case?
		if ( upper ) upperCount++;
	}

	sentflags_t sflags = 0;

	if ( lowerCount > 0 ) sflags |= SENT_MIXED_CASE_STRICT;
	
	if ( lowerCount == 1 && upperCount >= 2 ) lowerCount = 0;


	// . fix "7-10:30pm Contra dance"
	// . treat a numeric date like an upper case word
	if ( (bits[senta] & D_IS_IN_DATE) && 
	     // treat a single lower case word as error
	     lowerCount == 1 && 
	     // prevent "7:30-8:30 dance" for ceder.net i guess
	     upperCount >= 1)
		lowerCount = 0;

	if ( lowerCount > 0 ) sflags |= SENT_MIXED_CASE;

	return sflags;
}

bool Sections::setTableRowsAndCols ( Section *tableSec ) {

	//int32_t rowCount = -1;
	int32_t colCount = -1;
	int32_t maxColCount = -1;
	int32_t maxRowCount = -1;
	Section *headCol[100];
	Section *headRow[300];
	int32_t rowspanAcc[300];

	int32_t maxCol = -1;
	int32_t minCol =  99999;
	int32_t maxRow = -1;
	int32_t minRow =  99999;

	int32_t rowCount = 0;
	// init rowspan info
	for ( int32_t k = 0 ; k < 300 ; k++ ) rowspanAcc[k] = 1;

	Section *prev = NULL;
	Section *above[100];
	memset ( above,0,sizeof(Section *)*100);

	// scan sections in the table
	for ( Section *ss = tableSec->m_next ; ss ; ss = ss->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if went outside of table
		if ( ss->m_a >= tableSec->m_b ) break;
		// table in a table?
		Section *p = ss->m_parent;
		for ( ; p ; p = p->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( p->m_tagId == TAG_TABLE ) break;
		}
		// must not be within another table in our table
		if ( p != tableSec ) continue;
		// shortcut
		nodeid_t tid = ss->m_tagId;
		// . ok, we are a section the the table "tableSec" now
		// . row?
		if ( tid == TAG_TR ) {
			rowCount++;
			colCount = 0;
			continue;
		}
		// . column?
		// . fix eviesays.com which has "what" in a <th> tag
		if ( tid != TAG_TD && tid != TAG_TH ) continue;
		// . must have had a row
		if ( rowCount <= 0 ) continue;
		// . did previous td tag have multiple rowspan?
		// . this <td> tag is referring to the first column
		//   that has not exhausted its rowspan
		for ( ; colCount<300 ; colCount++ ) 
			if ( --rowspanAcc[colCount] <= 0 ) break;
		
		// is it wacko? we should check this in Dates.cpp
		// and ignore all dates in such tables or at least
		// not allow them to pair up with each other
		int32_t  rslen;
		char *rs = getFieldValue ( m_wptrs[ss->m_a] , // tag
					   m_wlens[ss->m_a] , // tagLen
					   "rowspan"  , 
					   &rslen );
		int32_t rowspan = 1;
		if ( rs ) rowspan = atol2(rs,rslen);
		//if ( rowspan != 1 ) 
		//	tableSec->m_flags |= SEC_WACKO_TABLE;
		if ( colCount < 300 )
			rowspanAcc[colCount] = rowspan;
		
		//Section *cs = m_sectionPtrs[i];
		// update headCol[] array to refer to us
		if ( rowCount == 1 && colCount < 100 ) {
			headCol[colCount] = ss;
			// record a max since some tables have
			// fewer columns in first row! bad tables!
			maxColCount = colCount;
		}
		// update headRow[] array to refer to us
		if ( colCount == 0 && rowCount < 300 ) {
			headRow[rowCount] = ss;
			// same for this
			maxRowCount = rowCount;
		}
		// set our junk
		if ( colCount < 100 && colCount <= maxColCount )
			ss->m_headColSection = headCol[colCount];
		if ( rowCount < 300 && rowCount <= maxRowCount )
			ss->m_headRowSection = headRow[rowCount];
		colCount++;
		// start counting at "1" so that way we know that a
		// Sections::m_col/rowCount of 0 is invalid
		ss->m_colNum   = colCount;
		ss->m_rowNum   = rowCount;
		ss->m_tableSec = tableSec;


		// . sets Section::m_cellAbove and Section::m_cellLeft to 
		//   point to the td or th cells above us or to the left of us
		//   respectively.
		// . use this to scan for dates when telescoping 
		// . if date is in a table and date you are telescoping to is 
		//   in the same table it must be to your left or above you in
		//   the same row/col if SEC_HASDATEHEADERROW or 
		//   SEC_HASDATEHEADERCOL is set for the table.
		// . so Dates::isCompatible() needs this function to set 
		//   those ptrs

		// who was on our left?
		if ( prev && prev->m_rowNum == rowCount )
			ss->m_leftCell = prev;
		// who is above us?
		if ( colCount<100 && rowCount>=2 && above[colCount] )
			ss->m_aboveCell = above[colCount];

		// update for row
		prev = ss;
		// update for col
		if ( colCount<100) above[colCount] = ss;

		// first word position in section. -1 if no words contained.
		if ( ss->m_firstWordPos >= 0 ) {
			if ( colCount > maxCol ) maxCol = colCount;
			if ( colCount < minCol ) minCol = colCount;
			if ( rowCount > maxRow ) maxRow = rowCount;
			if ( rowCount < minRow ) minRow = rowCount;
		}
		
		//
		// propagate to all child sections in our section
		//
		int32_t maxb = ss->m_b;
		Section *kid = ss->m_next;
		for ( ; kid && kid->m_b <= maxb ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if belongs to another table already
			if ( kid->m_tableSec &&
			     tableSec->contains ( kid->m_tableSec) ) 
				continue;
			// set his junk
			kid->m_colNum         = colCount;
			kid->m_rowNum         = rowCount;
			kid->m_headColSection = ss->m_headColSection;
			kid->m_headRowSection = ss->m_headRowSection;
			kid->m_tableSec       = ss->m_tableSec;
		}
	}

	// . require at least a 2x2!!!
	// . AND require there have been text in the other dimensions
	//   in order to fix the pool hours for www.the-w.org/poolsched.html
	//   TODO!!!
	// . TODO: allow addlist to combine dates across <td> or <th> tags
	//   provided the table is NOT SEC_MULTIDIMS...
	if ( maxRow != minRow && maxCol != minCol )
		tableSec->m_flags |= SEC_MULTIDIMS;

	return true;
}

// . "<table>" section tag is "sn"
// . set SEC_TABLE_HEADER on contained <td> or <th> cells that represent
//   a header column or row
bool Sections::setTableHeaderBits ( Section *ts ) {

	static char *s_tableFields [] = {
		// . these have no '$' so they are exact/full matches
		// . table column headers MUST match full matches and are not
		//   allowed to match substring matches
		"+who",
		"+what",
		"+event",
		"+title",

		// use '$' to endicate sentence ENDS in this
		"-genre",
		"-type",
		"-category",
		"-categories",
		"@where",
		"@location",
		"-contact", // contact: john
		"-neighborhood", // yelp uses this field
		"@venue",
		"-instructor",
		"-instructors",
		"-convenor", // graypanthers uses convenor:
		"-convenors", // graypanthers uses convenor:
		"-caller", // square dancing
		"-callers",
		"-call", // phone number
		"-price",
		"-price range",
		"@event location",
	};
	// store these words into field table "ft"
	static HashTableX s_ft;
	static char s_ftbuf[2000];
	static bool s_init0 = false;
	if ( ! s_init0 ) {
		s_init0 = true;
		s_ft.set(8,4,128,s_ftbuf,2000,false,m_niceness,"ftbuf");
		int32_t n = (int32_t)sizeof(s_tableFields)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_tableFields[i];
			Words w;
			w.set ( s , true, 0);

			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// . store hash of all words, value is ptr to it
			// . put all exact matches into ti1 and the substring
			//   matches into ti2
			if ( ! s_ft.addKey ( &h , &s ) ) {char *xx=NULL;*xx=0;}
		}
	}


	int32_t colVotes = 0;
	int32_t rowVotes = 0;
	int32_t maxCol = 0;
	int32_t maxRow = 0;
	Section *sn = ts;
	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// get max
		if ( sn->m_rowNum > maxRow ) maxRow = sn->m_rowNum;
		if ( sn->m_colNum > maxCol ) maxCol = sn->m_colNum;
		// must be row or col 1
		if ( sn->m_colNum != 1 && sn->m_rowNum != 1 ) continue;
		// header format?
		bool hdrFormatting = (sn->m_flags & SEC_HEADING_CONTAINER);
		// is it a single format? i.e. no word<tag>word in the cell?
		if ( sn->m_colNum == 1 && hdrFormatting ) colVotes++;
		if ( sn->m_rowNum == 1 && hdrFormatting ) rowVotes++;
		// skip if not heading container
		if ( ! hdrFormatting ) continue;
		// look for certain words like "location:" or "venue", those
		// are very strong indicators of a header row or header col
		for ( int32_t i = sn->m_a ; i < sn->m_b ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			if ( ! s_ft.isInTable ( &m_wids[i] ) ) continue;
			if ( sn->m_colNum == 1 ) colVotes += 10000;
			if ( sn->m_rowNum == 1 ) rowVotes += 10000;
		}
	}	

	bool colWins = false;
	bool rowWins = false;
	// colWins means col #1 is the table header
	if ( colVotes > rowVotes ) colWins = true;
	// rowWins means row #1 is the table header
	if ( colVotes < rowVotes ) rowWins = true;

	// do another scan of table
	sn = ts;
	// skip loop if no need
	if ( ! rowWins && ! colWins ) sn = NULL;
	// if table only has one row or col
	if ( maxRow <= 1 && maxCol <= 1 ) sn = NULL;

	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// it must be in the winning row or column
		if ( rowWins && sn->m_rowNum != 1 ) continue;
		if ( colWins && sn->m_colNum != 1 ) continue;
		// flag it as a table header
		sn->m_flags |= SEC_TABLE_HEADER;
		// propagate to all kids as well so the sentence itself
		// will have SEC_TABLE_HEADER set so we can detect that
		// in setSentFlags(), because we use it for setting
		// SENT_TAGS
		Section *kid = sn->m_next;
		for ( ; kid && kid->m_b <= sn->m_b ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if does not belong to our table
			if ( kid->m_tableSec &&
			     kid->m_tableSec != sn->m_tableSec ) continue;
			// set it
			kid->m_flags |= SEC_TABLE_HEADER;
		}
	}

	// scan the cells in the table, NULL out the 
	// m_headColSection or m_headRowSection depending
	sn = ts;
	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// if its a header section itself...
		if ( sn->m_flags & SEC_TABLE_HEADER ) {
			sn->m_headColSection = NULL;
			sn->m_headRowSection = NULL;
			// keep going so we can propagate the NULLs to our kids
		}
		// get its hdr
		Section *hdr = sn->m_headColSection;
		// only if we are > row 1
		//if ( sn->m_rowNum >= 2 ) hdr = sn->m_headColSection;
		// must have table header set
		if ( hdr && ! ( hdr->m_flags & SEC_TABLE_HEADER ) )
		     // but if we are not in the first col, we can use it
		     //sn->m_colNum == 1 ) 
			sn->m_headColSection = NULL;
		// same for row
		hdr = sn->m_headRowSection;
		// only if we are col > 1
		//if ( ! hdr && sn->m_colNum >= 2 ) hdr = sn->m_headRowSection;
		// must have table header set
		if ( hdr && ! ( hdr->m_flags & SEC_TABLE_HEADER ) )
		     // . but if we are not in the first row, we can use it
		     // . m_rowNum starts at 1, m_colNum starts at 1
		     //sn->m_rowNum == 1 ) 
			sn->m_headRowSection = NULL;

		//
		// propagate to all child sections in our section
		//
		Section *kid = sn->m_next;
		for ( ; kid && kid->m_b <= sn->m_b ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if does not belong to our table
			if ( kid->m_tableSec != ts ) continue;
			// set his junk
			//kid->m_colNum         = sn->m_colNum;
			//kid->m_rowNum         = sn->m_rowNum;
			kid->m_headColSection = sn->m_headColSection;
			kid->m_headRowSection = sn->m_headRowSection;
			//kid->m_tableSec       = sn->m_tableSec;
		}
	}

	return true;
}

bool Sections::growSections ( ) {
	// make a log note b/c this should not happen a lot because it's slow
	log("build: growing sections!");
	g_errno = EDOCBADSECTIONS;
	return true;
//	// record old buf start
//	char *oldBuf = m_sectionBuf.getBufStart();
//	// grow by 20MB at a time
//	if ( ! m_sectionBuf.reserve ( 20000000 ) ) return false;
//	// for fixing ptrs:
//	char *newBuf = m_sectionBuf.getBufStart();
//	// set the new max
//	m_maxNumSections = m_sectionBuf.getCapacity() / sizeof(Section);
//	// update ptrs in the old sections
//	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
//		// breathe
//		QUICKPOLL(m_niceness);
//		Section *si = &m_sections[i];
//		if ( si->m_parent ) {
//			char *np = (char *)si->m_parent;
//			np = np - oldBuf + newBuf;
//			si->m_parent = (Section *)np;
//		}
//		if ( si->m_next ) {
//			char *np = (char *)si->m_next;
//			np = np - oldBuf + newBuf;
//			si->m_next = (Section *)np;
//		}
//		if ( si->m_prev ) {
//			char *np = (char *)si->m_prev;
//			np = np - oldBuf + newBuf;
//			si->m_prev = (Section *)np;
//		}
//		if ( si->m_listContainer ) {
//			char *np = (char *)si->m_listContainer;
//			np = np - oldBuf + newBuf;
//			si->m_listContainer = (Section *)np;
//		}
//		if ( si->m_prevBrother ) {
//			char *np = (char *)si->m_prevBrother;
//			np = np - oldBuf + newBuf;
//			si->m_prevBrother = (Section *)np;
//		}
//		if ( si->m_nextBrother ) {
//			char *np = (char *)si->m_nextBrother;
//			np = np - oldBuf + newBuf;
//			si->m_nextBrother = (Section *)np;
//		}
//		if ( si->m_sentenceSection ) {
//			char *np = (char *)si->m_sentenceSection;
//			np = np - oldBuf + newBuf;
//			si->m_sentenceSection = (Section *)np;
//		}
//		if ( si->m_prevSent ) {
//			char *np = (char *)si->m_prevSent;
//			np = np - oldBuf + newBuf;
//			si->m_prevSent = (Section *)np;
//		}
//		if ( si->m_nextSent ) {
//			char *np = (char *)si->m_nextSent;
//			np = np - oldBuf + newBuf;
//			si->m_nextSent = (Section *)np;
//		}
//		if ( si->m_tableSec ) {
//			char *np = (char *)si->m_tableSec;
//			np = np - oldBuf + newBuf;
//			si->m_tableSec = (Section *)np;
//		}
//		if ( si->m_headColSection ) {
//			char *np = (char *)si->m_headColSection;
//			np = np - oldBuf + newBuf;
//			si->m_headColSection = (Section *)np;
//		}
//		if ( si->m_headRowSection ) {
//			char *np = (char *)si->m_headRowSection;
//			np = np - oldBuf + newBuf;
//			si->m_headRowSection = (Section *)np;
//		}
//		if ( si->m_leftCell ) {
//			char *np = (char *)si->m_leftCell;
//			np = np - oldBuf + newBuf;
//			si->m_leftCell = (Section *)np;
//		}
//		if ( si->m_aboveCell ) {
//			char *np = (char *)si->m_aboveCell;
//			np = np - oldBuf + newBuf;
//			si->m_aboveCell = (Section *)np;
//		}
//	}
//	return true;
}

