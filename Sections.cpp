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

// returns false and sets g_errno on error
bool Sections::setSentFlagsPart2 ( ) {

	static bool s_init2 = false;
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
	static int64_t h_login ;
	static int64_t h_back ;
	static int64_t h_change ;
	static int64_t h_write ;
	static int64_t h_save ;
	static int64_t h_share ;
	static int64_t h_forgot ;
	static int64_t h_home ;
	static int64_t h_hours;
	static int64_t h_sitemap ;
	static int64_t h_advanced ;
	static int64_t h_go ;
	static int64_t h_website ;
	static int64_t h_view;
	static int64_t h_add;
	static int64_t h_submit;
	static int64_t h_get;
	static int64_t h_subscribe;
	static int64_t h_loading;
	static int64_t h_last;
	static int64_t h_modified;
	static int64_t h_updated;
	static int64_t h_special;
	static int64_t h_guest ;
	static int64_t h_guests ;
	static int64_t h_directed;
	static int64_t h_venue ;
	static int64_t h_instructor ;
	static int64_t h_general; 
	static int64_t h_information; 
	static int64_t h_info ;
	static int64_t h_i ;
	static int64_t h_what ;
	static int64_t h_who ;
	static int64_t h_tickets; 
	static int64_t h_support ;
	static int64_t h_featuring;
	static int64_t h_presents;
	static int64_t h_phone;
	static int64_t h_usa;
	static int64_t h_relevancy; // sort by option
	static int64_t h_buy;
	static int64_t h_where;
	static int64_t h_when;
	static int64_t h_contact;
	static int64_t h_description;
	static int64_t h_location;
	static int64_t h_located;
	static int64_t h_of;
	static int64_t h_the;
	static int64_t h_and;
	static int64_t h_at;
	static int64_t h_to;
	static int64_t h_be;
	static int64_t h_or;
	static int64_t h_not;
	static int64_t h_in;
	static int64_t h_on;
	static int64_t h_for;
	static int64_t h_with;
	static int64_t h_from;
	static int64_t h_click;
	static int64_t h_here;
	static int64_t h_new;
	static int64_t h_free;
	static int64_t h_title;
	static int64_t h_event;
	static int64_t h_adv;
	static int64_t h_dos;
	static int64_t h_advance;
	static int64_t h_day;
	static int64_t h_show;
	static int64_t h_box;
	static int64_t h_office;
	static int64_t h_this;
	static int64_t h_week;
	static int64_t h_tonight;
	static int64_t h_today;
	static int64_t h_http;
	static int64_t h_https;
	static int64_t h_claim;
	static int64_t h_it;
	static int64_t h_upcoming;
	static int64_t h_events;
	static int64_t h_is;
	static int64_t h_your;
	static int64_t h_user;
	static int64_t h_reviews;
	static int64_t h_comments;
	static int64_t h_bookmark;
	static int64_t h_creator;
	static int64_t h_tags;
	static int64_t h_repeats;
	static int64_t h_feed;
	static int64_t h_readers;
	static int64_t h_no;
	static int64_t h_rating;
	static int64_t h_publish;
	static int64_t h_category;
	static int64_t h_genre;
	static int64_t h_type;
	static int64_t h_price;
	static int64_t h_rate;
	static int64_t h_rates;
	static int64_t h_users;

	static int64_t h_date ;
	static int64_t h_time ;
	static int64_t h_other ;
	static int64_t h_future ;
	static int64_t h_dates ;
	static int64_t h_times ;
	static int64_t h_hide ;
	static int64_t h_print ;
	static int64_t h_powered;
	static int64_t h_provided;
	static int64_t h_admission;
	static int64_t h_by;
	static int64_t h_com;
	static int64_t h_org;
	static int64_t h_net;
	static int64_t h_pg;
	static int64_t h_pg13;

	static int64_t h_a;
	static int64_t h_use;
	static int64_t h_search;
	static int64_t h_find;
	static int64_t h_school;
	static int64_t h_shop;
	static int64_t h_gift;
	static int64_t h_gallery;
	static int64_t h_library;
	static int64_t h_photo;
	static int64_t h_image;
	static int64_t h_picture;
	static int64_t h_video;
	static int64_t h_media;
	static int64_t h_copyright;
	static int64_t h_review;
	static int64_t h_join;
	static int64_t h_request;
	static int64_t h_promote;
	static int64_t h_open;
	static int64_t h_house;
	static int64_t h_million;
	static int64_t h_billion;
	static int64_t h_thousand;

	if ( ! s_init2 ) {
		s_init2 = true;
		h_repeats = hash64n("repeats");
		h_feed = hash64n("feed");
		h_readers = hash64n("readers");
		h_no = hash64n("no");
		h_rating = hash64n("rating");
		h_publish = hash64n("publish");
		h_category = hash64n("category");
		h_genre = hash64n("genre");
		h_type = hash64n("type");
		h_price = hash64n("price");
		h_rate = hash64n("rate");
		h_rates = hash64n("rates");
		h_users = hash64n("users");
		h_claim = hash64n("claim");
		h_it = hash64n("it");
		h_upcoming = hash64n("upcoming");
		h_events = hash64n("events");
		h_is = hash64n("is");
		h_your = hash64n("your");
		h_user = hash64n("user");
		h_reviews = hash64n("reviews");
		h_comments = hash64n("comments");
		h_bookmark = hash64n("bookmark");
		h_creator = hash64n("creator");
		h_close = hash64n("close");
		h_tags = hash64n("tags");
		h_send = hash64n("send");
		h_map = hash64n("map");
		h_maps = hash64n("maps");
		h_directions = hash64n("directions");
		h_driving = hash64n("driving");
		h_help = hash64n("help");
		h_more = hash64n("more");
		h_log = hash64n("log");
		h_sign = hash64n("sign");
		h_login = hash64n("login");
		h_back = hash64n("back");
		h_change = hash64n("change");
		h_write = hash64n("write");
		h_save = hash64n("save");
		h_add = hash64n("add");
		h_share = hash64n("share");
		h_forgot = hash64n("forgot");
		h_home = hash64n("home");
		h_hours = hash64n("hours");
		h_sitemap = hash64n("sitemap");
		h_advanced = hash64n("advanced");
		h_go = hash64n("go");
		h_website = hash64n("website");
		h_view = hash64n("view");
		h_submit = hash64n("submit");
		h_get = hash64n("get");
		h_subscribe = hash64n("subscribe");
		h_loading = hash64n("loading");
		h_last = hash64n("last");
		h_modified = hash64n("modified");
		h_updated = hash64n("updated");

		h_special = hash64n("special");
		h_guest = hash64n("guest");
		h_guests = hash64n("guests");
		h_directed = hash64n("directed");
		h_venue = hash64n("venue");
		h_instructor = hash64n("instructor");
		h_general = hash64n("general");
		h_information = hash64n("information");
		h_info = hash64n("info");
		h_what = hash64n("what");
		h_who = hash64n("who");
		h_tickets = hash64n("tickets");
		h_support = hash64n("support");
		h_featuring = hash64n("featuring");
		h_presents = hash64n("presents");

		h_phone = hash64n("phone");
		h_usa = hash64n("usa");
		h_relevancy = hash64n("relevancy");
		h_date = hash64n("date");
		h_description = hash64n("description");
		h_buy = hash64n("buy");
		h_where = hash64n("where");
		h_when = hash64n("when");
		h_contact = hash64n("contact");
		h_description = hash64n("description");
		h_location = hash64n("location");
		h_located = hash64n("located");
		h_of = hash64n("of");
		h_the = hash64n("the");
		h_and = hash64n("and");
		h_at = hash64n("at");
		h_to = hash64n("to");
		h_be = hash64n("be");
		h_or = hash64n("or");
		h_not = hash64n("not");
		h_in = hash64n("in");
		h_on = hash64n("on");
		h_for = hash64n("for");
		h_with = hash64n("with");
		h_from = hash64n("from");
		h_click = hash64n("click");
		h_here = hash64n("here");
		h_new = hash64n("new");
		h_free = hash64n("free");
		h_title = hash64n("title");
		h_event = hash64n("event");
		h_adv = hash64n("adv");
		h_dos = hash64n("dos");
		h_day = hash64n("day");
		h_show = hash64n("show");
		h_box = hash64n("box");
		h_i = hash64n("i");
		h_office = hash64n("office");
		h_this = hash64n("this");
		h_week = hash64n("week");
		h_tonight = hash64n("tonight");
		h_today = hash64n("today");
		h_http = hash64n("http");
		h_https = hash64n("https");
		h_date = hash64n("date");
		h_time = hash64n("time");
		h_other = hash64n("other");
		h_future = hash64n("future");
		h_dates = hash64n("dates");
		h_times = hash64n("times");
		h_hide = hash64n("hide");
		h_print = hash64n("print");
		h_powered = hash64n("powered");
		h_provided = hash64n("provided");
		h_admission = hash64n("admission");
		h_by = hash64n("by");
		h_com = hash64n("com");
		h_org = hash64n("org");
		h_net = hash64n("net");
		h_pg  = hash64n("pg");
		h_pg13  = hash64n("pg13");

		h_a = hash64n("a");
		h_use = hash64n("use");
		h_search = hash64n("search");
		h_find = hash64n("find");
		h_school = hash64n("school");
		h_shop = hash64n("shop");
		h_gift = hash64n("gift");
		h_gallery = hash64n("gallery");
		h_library = hash64n("library");
		h_photo = hash64n("photo");
		h_image = hash64n("image");
		h_picture = hash64n("picture");
		h_video = hash64n("video");
		h_media = hash64n("media");
		h_copyright = hash64n("copyright");
		h_review = hash64n("review");
		h_join = hash64n("join");
		h_request = hash64n("request");
		h_promote = hash64n("promote");
		h_open = hash64n("open");
		h_house = hash64n("house");
		h_million = hash64n("million");
		h_billion = hash64n("billion");
		h_thousand = hash64n("thousand");
	}

	// . title fields!
	// . if entire previous sentence matches this you are[not] a title
	// . uses + to mean is a title, - to mean is NOT a title following
	// . use '$' to indicate, "ends in that" (substring match)
	// . or if previous sentence ends in something like this, it is
	//   the same as saying that next sentence begins with this, that
	//   should fix "Presented By Colorado Symphony Orchestra" from
	//   being a good event title
	static char *s_titleFields [] = {

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
		"-advisor",
		"-advisors",
		"-leader",
		"-leaders",
		"-chair",
		"-chairman",
		"-chairperson",
		"-designer",
		"-designers",
		"-convenor", // graypanthers uses convenor:
		"-convenors", // graypanthers uses convenor:
		"-caller", // square dancing
		"-callers",
		"-call", // phone number
		"-price",
		"-price range",
		"@event location",

		// put colon after to indicate we need a colon after!
		// or this can be in a column header.
		// try to fix "business categories: " for switchboard.com
		//"-$categories:"

		// use '$' to endicate sentence ENDS in this
		"+$presents",
		"+$present",
		"+$featuring",

		// use '$' to endicate sentence ENDS in this
		"-$presented by",
		"-$brought to you by",
		"-$sponsored by",
		"-$hosted by" 
	};
	// store these words into table
	static HashTableX s_ti1;
	static HashTableX s_ti2;
	static char s_tibuf1[2000];
	static char s_tibuf2[2000];
	static bool s_init6 = false;
	if ( ! s_init6 ) {
		s_init6 = true;
		s_ti1.set(8,4,128,s_tibuf1,2000,false,m_niceness,"ti1tab");
		s_ti2.set(8,4,128,s_tibuf2,2000,false,m_niceness,"ti2tab");
		int32_t n = (int32_t)sizeof(s_titleFields)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_titleFields[i];
			Words w;
			w.set ( s, true, 0 );

			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// . store hash of all words, value is ptr to it
			// . put all exact matches into ti1 and the substring
			//   matches into ti2
			if ( s[1] != '$' )
				s_ti1.addKey ( &h , &s );
			else
				s_ti2.addKey ( &h , &s );
		}
	}

	// store these words into table
	if ( ! s_init3 ) initGenericTable ( m_niceness );

	//
	// phrase exceptions to the ignore words
	//
	static char *s_exceptPhrases [] = {
		// this title was used by exploratorium on a zvents page
		"free day",
		"concert band", // the abq concert band
		"band concert",
		"the voice", // "the voice of yes"
		"voice of", // maybe just eliminate voice by itself...?
		"voice for"
	};
	static HashTableX s_ext;
	static char s_ebuf[10000];
	static bool s_init4 = false;
	if ( ! s_init4 ) {
		s_init4 = true;
		s_ext.set(8,0,512,s_ebuf,10000,false,m_niceness,"excp-tab");
		int32_t n = (int32_t)sizeof(s_exceptPhrases)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			Words w;
			w.set ( s_exceptPhrases[i], true, 0 );

			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// store hash of all words
			s_ext.addKey ( &h );
		}
	}
	// shortcut
	Sections *ss = this;

	// init table
	HashTableX cht;
	char chtbuf[10000];
	cht.set(8,4,512,chtbuf,10000,false,m_niceness,"event-chash");
	// hash the content hash of each section and penalize the title
	// score if it is a repeat on this page
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no text
		//if ( ! si->m_contentHash ) continue;
		// get content hash
		int64_t ch64 = si->m_contentHash64;
		// fix for sentences
		if ( ch64 == 0 ) ch64 = si->m_sentenceContentHash64;
		// if not there in either one, skip it
		if ( ! ch64 ) continue;
		// combine the tag hash with the content hash #2 because
		// a lot of times it is repeated in like a different tag like
		// the title tag
		int64_t modified = si->m_tagHash ^ ch64;
		// store it. return false with g_errno set on error
		if ( ! cht.addTerm ( &modified ) ) return false;
	}

	// for checking if title contains phone #
	//HashTableX *pt = m_dates->getPhoneTable   ();		
	// shortcut
	wbit_t *bits = m_bits->m_bits;

	bool afterColon = false;
	Section *lastsi = NULL;
	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);

		// if tag breaks turn this off
		if ( afterColon &&
		     si->m_tagId &&  
		     isBreakingTagId ( si->m_tagId ) )
			afterColon = false;

		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		//if ( si->m_minEventId <= 0 ) continue;

		// if after a colon add that
		if ( afterColon )
			si->m_sentFlags |= SENT_AFTER_COLON;

		// now with our new logic in Sections::addSentence() [a,b) may
		// actually not be completely contained in "si". this fixes
		// such sentences in aliconference.com and abqtango.com
		int32_t senta = si->m_senta;
		int32_t sentb = si->m_sentb;

		////////////
		//
		// a byline for a quote? 
		//
		// fixes "a great pianist" -New York Times so we do not
		// get "New York Times" as the title for terrence-wilson
		//
		///////////
		char needChar = '-';
		bool over     = false;
		// word # senta must be alnum!
		if ( ! m_wids[senta] ) { char *xx=NULL;*xx=0; }
		// start our backwards scan right before the first word
		for ( int32_t i = senta - 1; i >= 0 ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// need punct
			if ( m_wids[i] ) {
				// no words allowed between hyphen and quote
				//if ( needChar == '-' ) continue;
				// otherwise if we had the hyphen and need
				// the quote, we can't have a word pop up here
				over = true;
				break;
			}
			if ( m_tids[i] ) continue;
			// got punct now
			char *pstart = m_wptrs[i];
			char *p      = m_wptrs[i] + m_wlens[i] - 1;
			for ( ; p >= pstart ; p-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				if ( is_wspace_a(*p) )
					continue;
				if ( *p != needChar ) { 
					over = true;
					break;
				}
				if ( needChar == '-' ) {
					needChar = '\"';
					continue;
				}
				// otherwise, we got it!
				si->m_sentFlags |= SENT_IS_BYLINE;
				over = true;
				break;
			}
			// stop if not byline or we got byline
			if ( over ) break;
		}
			

		// Tags:
		if ( sentb - senta == 1 && m_wids[senta] == h_tags )
			si->m_sentFlags |= SENT_TAG_INDICATOR;

		bool tagInd = false;
		if ( lastsi &&  (lastsi->m_sentFlags & SENT_TAG_INDICATOR) )
			tagInd = true;
		// tables: but if he was in different table row, forget it
		if ( tagInd && lastsi->m_rowNum != si->m_rowNum )
			tagInd = false;
		// tables: or same row, but table is not <= 2 columns
		if ( lastsi && (lastsi->m_flags & SEC_TABLE_HEADER) )
			tagInd = false;
		// if in a table, is our header a tags indicator?
		Section *hdr = si->m_headColSection;
		// must have table header set
		if ( ! hdr ) hdr = si->m_headRowSection;
		// check it out
		if ( hdr && 
		     hdr->m_nextSent &&
		     hdr->m_nextSent->m_a < hdr->m_b &&
		     (hdr->m_nextSent->m_sentFlags & SENT_TAG_INDICATOR) )
			tagInd = true;
		// ok, it was a tag indicator before, so we must be tags
		if ( tagInd )
			si->m_sentFlags |= SENT_TAGS;

		///////////////
		//
		// set D_IS_IN_PARENS
		//
		///////////////

		// sometimes title starts with a word and the ( or [
		// is before that word! so back up one word to capture it
		int32_t a = senta;
		// if prev word is punct back up
		if ( a-1>=0 && ! m_wids[a-1] && ! m_tids[a-1] ) a--;
		// backup over prev fron tag
		if ( a-1>=0 &&   m_tids[a-1] && !(m_tids[a-1]&BACKBIT) ) a--;
		// and punct
		if ( a-1>=0 && ! m_wids[a-1] && ! m_tids[a-1] ) a--;
		// init our flags
		//int32_t nonGenerics = 0;
		bool inParens = false;
		bool inSquare = false;

		for ( int32_t j = a ; j < sentb ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip non alnm wor
			if ( ! m_wids[j] ) {
				// skip tags
				if ( m_tids[j] ) continue;
				// count stuff in ()'s or []'s as generic
				if ( m_words->hasChar(j,')') ) inParens =false;
				if ( m_words->hasChar(j,'(') ) inParens =true;
				if ( m_words->hasChar(j,']') ) inSquare =false;
				if ( m_words->hasChar(j,'[') ) inSquare =true;
				continue;
			}
			// generic if in ()'s or []'s
			if ( inParens || inSquare ) {
				bits[j] |= D_IN_PARENS;
				continue;
			}
			// skip if in date
			if ( bits[j] & D_IS_IN_DATE ) {
				continue;
			}

			// numbers are generic (but not if contains an alpha)
			if ( m_words->isNum(j) ) {
				bits[j] |= D_IS_NUM;
				continue;
			}
			// hex num?
			if ( m_words->isHexNum(j) ) {
				bits[j] |= D_IS_HEX_NUM;
				continue;
			}
		}

		//int32_t upperCount = 0;
		int32_t alphas = 0;
		bool lastStop = false;
		bool inDate = true;
		int32_t stops = 0;
		inParens = false;
		int32_t dollarCount = 0;
		int32_t priceWordCount = 0;

		// watchout if in a table. the last table column header
		// should not be applied to the first table cell in the
		// next row! was screwing up
		// www.carnegieconcerts.com/eventperformances.asp?evt=54
		if ( si->m_tableSec &&
		     lastsi &&
		     lastsi->m_tableSec == si->m_tableSec &&
		     lastsi->m_rowNum != si->m_rowNum &&
		     lastsi->m_colNum > 1 )
			lastsi = NULL;

		////////////////////
		//
		// if prev sentence had a title field set, adjust us!
		//
		////////////////////
		if ( lastsi && (lastsi->m_sentFlags & SENT_NON_TITLE_FIELD))
			si->m_sentFlags |= SENT_INNONTITLEFIELD;
		if ( lastsi && (lastsi->m_sentFlags & SENT_TITLE_FIELD) )
			si->m_sentFlags |= SENT_INTITLEFIELD;
		// we are the new lastsi now
		lastsi = si;



		//////////////////
		//
		// set SENT_NON_TITLE_FIELD
		//
		// - any sentence immediately following us will get its
		//   title score reduced and SENT_INNONTITLEFIELD flag set
		//   if we set this SENT_INNONTITLEFIELD for this sentence
		//
		////////////////
		int64_t h = 0;
		int32_t wcount = 0;
		// scan BACKWARDS
		for ( int32_t i = sentb - 1 ; i >= senta ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// must have word after at least
			if ( i + 2 >= m_nw ) continue;
			// set it first time?
			if ( h == 0 ) h  = m_wids[i];
			else          h ^= m_wids[i];
			// max word count
			if ( ++wcount >= 5 ) break;
			// get from table
			char **msp1 = (char **)s_ti1.getValue(&h);
			char **msp2 = (char **)s_ti2.getValue(&h);
			// cancel out msp1 (exact match) if we do not have
			// the full sentence hashed yet
			if ( i != senta ) msp1 = NULL;
			// if we are doing a substring match we must be
			// filled with generic words! otherwise we get
			// "...permission from the Yoga Instructor."
			// becoming a non-title field and hurting the next
			// sentence's capaiblity of being a title.
			//if ( !(si->m_sentFlags & SENT_GENERIC_WORDS) )
			//msp2 = NULL;
			// if not in table,s kip
			if ( ! msp1 && ! msp2 ) continue;
			char *s = NULL;
			// use exact match first if we got it
			if ( msp1 ) s = *msp1;
			// otherwise, use the substring match
			else        s = *msp2;

			// Fix: "Sort by: Date | Title | Photo" so Title
			// is not a title field in this case. scan for
			// | after the word.
			int32_t pcount = 0;
			bool hadPipeAfter = false;
			for ( int32_t j = i + 1 ; j < m_nw ; j++ ) {
				QUICKPOLL(m_niceness);
				if ( m_tids[j] ) continue;
				if ( m_wids[j] ) break;
				char *p    = m_wptrs[j];
				char *pend = p + m_wlens[j];
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p == '|' ) break;
				}
				if ( p < pend ) {
					hadPipeAfter = true;
					break;
				}
				// scan no more than two punct words
				if ( ++pcount >= 2 ) break;
			}
			// if we hit the '|' then forget it!
			if ( hadPipeAfter ) continue;
			// we matched then
			if ( s[0] == '+' )
				si->m_sentFlags |= SENT_TITLE_FIELD;
			else
				si->m_sentFlags |= SENT_NON_TITLE_FIELD;
			// @Location
			//if ( s[0] == '@' )
			//	si->m_sentFlags |= SENT_PLACE_FIELD;
			break;
		}

		//////////////////////////
		//
		// USE TABLE HEADERS AS INDICATORS
		//
		//
		// . repeat that same logic but for the table column header
		// . if we are in a table and table header is "title"
		// . for http://events.mapchannels.com/Index.aspx?venue=628
		// . why isn't this kicking in for psfs.org which has
		//   "location" in the table column header
		// . we set "m_headColSection" to NULL if not a header per se
		// . a table can only have a header row or header column

		hdr = si->m_headColSection;
		// must have table header set
		if ( ! hdr ) hdr = si->m_headRowSection;

		// ok, set to it
		int32_t csentb = 0;
		int32_t csenta = 0;
		if ( hdr && hdr->m_firstWordPos >= 0 && 
		     // do not allow the the header row to get its
		     // SENT_PLACE_INDICATED set, etc. it's a field not a value
		     ! hdr->contains ( si ) ) {
			csenta = hdr->m_firstWordPos;
			csentb = hdr->m_lastWordPos+1;
		}
		h = 0;
		wcount = 0;
		// scan BACKWARDS
		for ( int32_t i = csentb - 1 ; i >= csenta ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// set it first time?
			if ( h == 0 ) h  = m_wids[i];
			else          h ^= m_wids[i];
			// max word count
			if ( ++wcount >= 5 ) break;
			// stop if not full yet
			if ( i != csenta ) continue;
			// get from table, only exact match since we are
			// checking column headers
			char **msp1 = (char **)s_ti1.getValue(&h);
			//char **msp2 = (char **)s_ti1.getValue(j);
			// the full sentence hashed yet
			if ( ! msp1 ) continue;
			// use exact match first if we got it
			char *s = *msp1;
			// we matched then
			if ( s[0] == '+' ) {
				si->m_sentFlags |= SENT_INTITLEFIELD;
			}
			else {
				si->m_sentFlags |= SENT_INNONTITLEFIELD;
			}
			// @Location, like for psfs.org has Location in tablcol
			if ( s[0] == '@' )
				si->m_sentFlags |= SENT_INPLACEFIELD;
			break;
		}
		     
		bool hadDollar = false;
		// fix sentences that start with stuff like "$12 ..."
		if ( senta>0 && 
		     ! m_tids[senta-1] &&
		     m_words->hasChar(senta-1,'$') ) {
			dollarCount++;
			hadDollar = true;
		}

		bool hasSpace = false;

		//////////////////
		//
		// SENT_TAGS
		//
		//////////////////
		//
		// . just check for <eventTags> for now
		// . get parent tag
		Section *ps = si->m_parent;
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],"<eventTags>",11)==0 )
			si->m_sentFlags |= SENT_TAGS;

		/////////////////
		//
		// SENT_INTITLEFIELD
		//
		/////////////////
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],"<eventTitle>",12)==0 )
			si->m_sentFlags |= SENT_INTITLEFIELD;
		// stubhub.com feed support
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],
				 "<str name=\"act_primary\">",24)==0 )
			si->m_sentFlags |= SENT_INTITLEFIELD;


		///////////////////
		//
		// SENT_STRANGE_PUNCT etc.
		//
		///////////////////
		int64_t lastWid = 0LL;
		for ( int32_t i = senta ; i < sentb ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) {
				// skip tags right away
				if ( m_tids[i] ) continue;
				char *p       = m_wptrs[i];
				char *pend    = p + m_wlens[i];
				bool  strange = false;
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p == '$' ) { 
						hadDollar = true;
						dollarCount++;
					}
					if ( *p == '=' ) strange = true;
					if ( *p == '*' ) strange = true;
					if ( *p == '%' ) strange = true;
					if ( is_wspace_a(*p) ) hasSpace = true;
					// following meats: ____chicken ___pork
					if ( *p == '_' && p[1] == '_' )
						strange = true;
					// need one alnum b4 parens check
					if ( alphas == 0 ) continue;
					if ( *p == '(' ) inParens = true;
					if ( *p == ')' ) inParens = false;
				}
				if ( strange )
					si->m_sentFlags |= SENT_STRANGE_PUNCT;
				// need at least one alnum before parens check
				if ( alphas == 0 ) continue;
				// has colon
				if ( m_words->hasChar(i,':') &&
				     i>senta && 
				     ! is_digit(m_wptrs[i][-1]) )
					si->m_sentFlags |= SENT_HAS_COLON;
				// check for end first in case of ") ("
				//if ( m_words->hasChar(i,')') )
				//	inParens = false;
				// check if in parens
				//if ( m_words->hasChar(i,'(') )
				//	inParens = true;
				continue;
			}

			//
			// check for a sane PRICE after the dollar sign.
			// we do not want large numbers after it, like for
			// budgets or whatever. those are not tickets!!
			//
			if ( hadDollar ) {
				// require a number after the dollar sign
				if ( ! is_digit(m_wptrs[i][0]) ) 
					dollarCount = 0;
				// number can't be too big!
				char *p = m_wptrs[i];
				int32_t digits =0;
				char *pmax = p + 30;
				bool hadPeriod = false;
				for ( ; *p && p < pmax ; p++ ) {
					if ( *p == ',' ) continue;
					if ( *p == '.' ) {
						hadPeriod = true;
						continue;
					}
					if ( is_wspace_a(*p) ) continue;
					if ( is_digit(*p) ) {
						if ( ! hadPeriod ) digits++;
						continue;
					}
					// $20M? $20B?
					if ( to_lower_a(*p) == 'm' )
						dollarCount = 0;
					if ( to_lower_a(*p) == 'b' ) 
						dollarCount = 0;
					break;
				}
				if ( digits >= 4 ) 
					dollarCount = 0;
				// if word after the digits is million
				// thousand billion, etc. then nuke it
				int32_t n = i + 1;
				int32_t nmax = i + 10;
				if ( nmax > m_nw ) nmax = m_nw;
				for ( ; n < nmax ; n++ ) {
					if ( ! m_wids[n] ) continue;
					if ( m_wids[n] == h_million )
						dollarCount = 0;
					if ( m_wids[n] == h_billion )
						dollarCount = 0;
					if ( m_wids[n] == h_thousand )
						dollarCount = 0;
					break;
				}
				// reset for next one
				hadDollar = false;
			} 

			int64_t savedWid = lastWid;
			lastWid = m_wids[i];

			// . skip if in parens
			// . but allow place name #1's in parens like
			//   (Albuquerque Rescue Mission) is for unm.edu
			if ( inParens ) continue;

			// . in date?
			// . hurts "4-5pm Drumming for Dancers w/ Heidi" so
			//   make sure all are in date!
			if ( ! ( bits[i] & D_IS_IN_DATE ) ) inDate = false;
			// "provided/powered by"
			if ( ( m_wids[i] == h_provided ||
			       m_wids[i] == h_powered ) &&
			     i + 2 < sentb &&
			     m_wids[i+2] == h_by )
				si->m_sentFlags |= SENT_POWERED_BY;
			// pricey? "free admission" is a price... be sure
			// to include in the description!
			if ( m_wids[i] == h_admission && savedWid == h_free )
				si->m_sentFlags |= SENT_HAS_PRICE;
			// count alphas
			if ( ! is_digit(m_wptrs[i][0]) ) alphas++;
			// "B-52" as in the band (exclude phone #'s!)
			else if ( i-2>0 && 
				  m_wptrs[i][-1] =='-' &&
				  !is_digit(m_wptrs[i-2][0] ) )
				  alphas++;
			// "B-52s", 52s has a letter in it!
			else if ( ! m_words->isNum(i) ) alphas++;
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
			// count them
			if ( isStopWord ) stops++;

			// if we end on a stop word that is usually indicative
			// of something like
			// "Search Results for <h1>Doughnuts</h1>" as for
			// switchborad.com url
			if ( m_wids[i] == h_of ||
			     m_wids[i] == h_the ||
			     m_wids[i] == h_and ||
			     m_wids[i] == h_at  ||
			     m_wids[i] == h_to  ||
			     m_wids[i] == h_be  ||
			     m_wids[i] == h_or  ||
			     m_wids[i] == h_not ||
			     m_wids[i] == h_in  ||
			     m_wids[i] == h_by  ||
			     m_wids[i] == h_on  ||
			     m_wids[i] == h_for ||
			     m_wids[i] == h_with||
			     m_wids[i] == h_from )
				lastStop = true;
			else
				lastStop = false;
			// ticket pricey words
			if ( m_wids[i] == h_adv ||
			     m_wids[i] == h_dos ||
			     m_wids[i] == h_advance ||
			     m_wids[i] == h_day ||
			     m_wids[i] == h_of ||
			     m_wids[i] == h_show ||
			     m_wids[i] == h_box ||
			     m_wids[i] == h_office )
				priceWordCount++;
		}

		// set this
		if ( ! hasSpace ) 
			si->m_sentFlags |= SENT_HASNOSPACE;

		// try to avoid mutiple-ticket- price titles
		if ( dollarCount >= 2 )
			si->m_sentFlags |= SENT_PRICEY;
		// . if all words in section are describing ticket price...
		// . fix bad titles for southgatehouse.com because right title
		//   is in SEC_MENU and perchance is also repeated on the page
		//   so its score gets slammed and it doesn't even make it in
		//   the event description. instead the title we pick is this
		//   ticket pricey title, so let's penalize that here so the
		//   right title comes through
		// . pricey title was "$17 ADV / $20 DOS"
		// . title in SEC_MENU is now "Ricky Nye". we should have
		//   "Buckwheat zydeco" too, but i guess it didn't make the
		//   cut, but since we set EV_OUTLINKED_TITLE anyway, it 
		//   doesn't matter for now.
		if ( dollarCount >= 1 && alphas == priceWordCount )
			si->m_sentFlags |= SENT_PRICEY;
		// single dollar sign?
		if ( dollarCount >= 1 )
			si->m_sentFlags |= SENT_HAS_PRICE;

		// . if ALL words in date, penalize
		// . penalize by .50 to fix events.mapchannels.com
		// . nuke even harder because www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer
		//   was using the date of the events as the title rather than
		//   resorting to "Other Future Dates & Times" header
		//   which was penalized down to 4.7 because of MULT_EVENTS
		if ( inDate ) {
			si->m_sentFlags |= SENT_IS_DATE;
		}

		// hurts "Jay-Z" but was helping "Results 1-10 of"
		if ( lastStop )
			si->m_sentFlags |= SENT_LAST_STOP;

		// punish if only wnumbers (excluding stop words)
		if ( alphas - stops == 0 )
			si->m_sentFlags |= SENT_NUMBERS_ONLY;

		//
		// end case check
		//

		char *p = NULL;
		int32_t lastPunct = si->m_sentb;
		// skip over tags to fix nonamejustfriends.com sentence
		for ( ; lastPunct < m_nw && m_tids[lastPunct] ; lastPunct++);
		// now assume, possibly incorrectly, that it is punct
		if ( lastPunct < m_nw ) p = m_wptrs[lastPunct];


		if ( p && (p[0]==':' || p[1]==':' ) ) {
		     // commas also need to fix lacma.org?
		     //!(si->m_sentFlags & SENT_HAS_COMMA) &&
		     // . only set this if we are likely a field name
		     // . fix "Members of the Capitol Ensemble ...
		     //   perform Schubert: <i>String Trio in B-flat..."
		     //   for lacma.org
		     //(si->m_alnumPosB - si->m_alnumPosA) <= 5 ) {
			si->m_sentFlags |= SENT_COLON_ENDS;
			afterColon = true;
		}
		// starts with '(' or '[' is strange!
		if ( senta>0 && 
		     ( m_wptrs[senta][-1] == '(' ||
		       m_wptrs[senta][-1] == '[' ) )
			si->m_sentFlags |= SENT_PARENS_START; // STRANGE_PUNCT;

		// if in a tag of its own that's great! like being in a header
		// tag kind of
		Section *sp = si->m_parent;
		// skip paragraph tags
		//if ( sp && sp->m_tagId == TAG_P ) sp = sp->m_parent;

		if ( sp &&
		     //sp->m_tagId != TAG_P &&
		     sp->m_firstWordPos == si->m_firstWordPos &&
		     sp->m_lastWordPos  == si->m_lastWordPos ) 
			si->m_sentFlags |= SENT_IN_TAG;

		// this is obvious
		if ( si->m_sentFlags & SENT_IN_HEADER )
			si->m_sentFlags |= SENT_IN_TAG;

	}

	//bool lastSentenceHadColon = false;
	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;

		if ( si->m_flags & SEC_MENU )
			si->m_sentFlags |= SENT_IN_MENU;

		if ( si->m_flags & SEC_MENU_SENTENCE )
			si->m_sentFlags |= SENT_MENU_SENTENCE;

		// why not check menu header too?
		// would fix 'nearby bars' for terrence-wilson zvents url
		if ( si->m_flags & SEC_MENU_HEADER )
			si->m_sentFlags |= SENT_IN_MENU_HEADER;

		int32_t sa = si->m_senta;
		int32_t sb = si->m_sentb;

		// if breaking tag between "sa" and last word of prev sentence
		// AND now breaking tag between our last word and beginning
		// of next sentence, that's a "word sandwich" and not 
		// conducive to titles... treat format change tags as 
		// breaking tags for this purpose...
		int32_t na = sb;
		int32_t maxna = na + 40;
		if ( maxna > m_nw ) maxna = m_nw;
		bool hadRightTag = true;
		for ( ; na < maxna ; na++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop on tag
			if ( m_tids[na] ) break;
			// or word
			if ( ! m_wids[na] ) continue;
			// heh...
			hadRightTag = false;
			break;
		}
		bool hadLeftTag = true;
		na = sa - 1;
		int32_t minna = na - 40;
		if ( minna < 0 ) minna = 0;
		for ( ; na >= minna ; na-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if had a tag on right
			if ( hadRightTag ) break;
			// stop on tag
			if ( m_tids[na] ) break;
			// or word
			if ( ! m_wids[na] ) continue;
			// heh...
			hadLeftTag = false;
			break;
		}
		if ( ! hadRightTag && ! hadLeftTag )
			si->m_sentFlags |= SENT_WORD_SANDWICH;

		// <hr> or <p>...</p> before us with no text?
		Section *pj = si->m_prev;
		// keep backing up until does not contain us, if ever
		for ( ; pj && pj->m_b > sa ; pj = pj->m_prev ) {
			// breathe
			QUICKPOLL(m_niceness);
		}
		// now check it out
		if ( pj && pj->m_firstWordPos < 0 )
			// must be p or hr tag etc.
			si->m_sentFlags |= SENT_AFTER_SPACER;

		// TODO: also set if first sentence in CONTAINER...!!

		

		// likewise, before a spacer tag
		pj = si->m_next;
		// keep backing up until does not contain us, if ever
		for ( ; pj && pj->m_a < sb ; pj = pj->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
		}
		// now check it out
		if ( pj && pj->m_firstWordPos < 0 )
			// must be p or hr tag etc.
			si->m_sentFlags |= SENT_BEFORE_SPACER;

		// . second title slam
		// . need to telescope up for this i think
		Section *sit = si;
		for ( ; sit ; sit = sit->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// test
			if ( ! ( sit->m_flags & SEC_SECOND_TITLE ) ) continue;
			si->m_sentFlags |= SENT_SECOND_TITLE;
			break;
		}

		// are we in a facebook name tag?
		sit = si;
		if ( m_contentType != CT_XML ) sit = NULL;
		for ( ; sit ; sit = sit->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// are we in a facebook <name> tag? that is event title
			if ( m_isFacebook && sit->m_tagId == TAG_FBNAME ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// for trumba and eventbrite... etc.
			if ( sit->m_tagId == TAG_GBXMLTITLE ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// stop if hit an xml tag. we do not support nested
			// tags for this title algo. 
			if ( sit->m_tagId ) break;
		}

		// . in header tag boost
		// . should beat noprev/nonextbrother combo
		// . fixes meetup.com
		if ( si->m_flags & SEC_IN_HEADER )
			si->m_sentFlags |= SENT_IN_HEADER;

		// . now fix trumba.com which has <title> tag for each event
		// . if parent section is title tag or has "title" in it
		//   somewhere, give us a boost
		Section *ip = si->m_parent;
		// if counted as header do not re-count as title too
		//if ( si->m_sentFlags & SENT_IN_HEADER ) ip = NULL;
		// ignore <title> tags if we are not an rss feed (trumba fix)
		if ( ip && ip->m_tagId == TAG_TITLE && ! m_isRSSExt) ip = NULL;
		// keep telescoping up as int32_t as parent just contains this
		// sentence, si.
		for ( ; ip ; ip = ip->m_parent ) {
			// parent must only contain us
			if ( ip->m_firstWordPos != si->m_firstWordPos ) break;
			if ( ip->m_lastWordPos  != si->m_lastWordPos  ) break;
			// do not allow urls that could have "title" in them
			if ( ip->m_tagId == TAG_A      ) break;
			if ( ip->m_tagId == TAG_IFRAME ) break;
			if ( ip->m_tagId == TAG_FRAME  ) break;
			// trumba title tag? need for eventbrite too!
			if ( ip->m_tagId == TAG_GBXMLTITLE ) {//&& isTrumba ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// get tag limits
			char *ta = m_wptrs[ip->m_a];
			char *tb = m_wlens[ip->m_a] + ta;
			// scan for "title"
			char *ss = gb_strncasestr(ta,tb-ta,"title") ;
			// skip if not there
			if ( ! ss ) break;
			// . stop if has equal sign after
			// . exempts "<div title="blah">" to fix 
			//   reverbnation.com from getting Lyrics as a title
			//   for an event
			if ( ss[5] == '=' ) break;
			// reward
			//tflags |= SENT_IN_TITLEY_TAG;
			// if we are trumba, we trust these 100% so
			// make sure it is the title. but if we
			// have multiple candidates we still want to
			// rank them amongst themselves so just give
			// a huge boost. problem was was that some event
			// items repeated the event date in the brother 
			// section below the address section, thus causing the
			// true event title to get SENT_MULT_EVENTS set while
			// the address place name would be selected as the
			// title for one, because the address section also
			// contained the event time. and the other "event"
			// would use a title from its section.
			//if ( isTrumba ) {
			//	si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
			//	//tscore = (tscore + 100.0) * 2000.0;
			//	//dscore = (dscore + 100.0) * 2000.0;
			//}
			//else {
			si->m_sentFlags |= SENT_IN_TITLEY_TAG;
			//}
			// once is good enough
			break;
		}

		int64_t ch64 = si->m_contentHash64;
		// fix for sentences
		if ( ch64 == 0 ) ch64 = si->m_sentenceContentHash64;
		// must be there
		if ( ! ch64 ) { char *xx=NULL;*xx=0; }
		// combine the tag hash with the content hash #2 because
		// a lot of times it is repeated in like a different tag like
		// the title tag
		int64_t modified = si->m_tagHash ^ ch64;
		// repeat on page?
		// hurts "5:30-7pm Beginning African w/ Romy" which is
		// legit and repeated for different days of the week for
		// texasdrums.drums.org, so ease off a bit
		int32_t chtscore = cht.getScore ( &modified ) ;
		if ( chtscore > 1 ) 
			si->m_sentFlags |= SENT_PAGE_REPEAT;

		int32_t f = si->m_senta;//si->m_firstWordPos;
		int32_t L = si->m_sentb;//si->m_lastWordPos;
		if ( f < 0 ) { char *xx=NULL;*xx=0; }
		if ( L < 0 ) { char *xx=NULL;*xx=0; }
		// single word?
		bool single = (f == (L-1));

		// slight penalty if first word is action word or another
		// word not very indicative of a title, and more indicative
		// of a menu element
		bool badFirst = false;
		bool skip = false;
		if ( m_wids[f] == h_send  ) badFirst = true;
		if ( m_wids[f] == h_save  ) badFirst = true;
		if ( m_wids[f] == h_add   ) badFirst = true;
		if ( m_wids[f] == h_share ) badFirst = true;
		if ( m_wids[f] == h_join  ) badFirst = true;
		// request a visit from jim hammond: booktour.com
		if ( m_wids[f] == h_request ) badFirst = true;
		if ( m_wids[f] == h_contact ) badFirst = true;
		// "promote your event" : booktour.com
		if ( m_wids[f] == h_promote  ) badFirst = true;
		if ( m_wids[f] == h_subscribe ) badFirst = true;
		if ( m_wids[f] == h_loading   ) badFirst = true;
		// "last modified|updated"
		if ( m_wids[f] == h_last && f+2 < m_nw &&
		     ( m_wids[f+2] == h_modified || m_wids[f+2]==h_updated)) 
			badFirst = true;
		// special guest
		if ( m_wids[f] == h_special && f+2 < m_nw &&
		     ( m_wids[f+2] == h_guest || m_wids[f+2]==h_guests)) 
			badFirst = true;
		// directed by ... darren dunbar (santafeplayhouse.org)
		if ( m_wids[f] == h_directed && f+2 < m_nw &&
		     m_wids[f+2] == h_by ) 
			badFirst = true;
		// map of
		if ( m_wids[f] == h_map && f+2 < m_nw &&
		     m_wids[f+2] == h_of ) 
			badFirst = true;
		// "more|other|venue information|info"
		if ( ( m_wids[f] == h_more  || 
		       m_wids[f] == h_venue || 
		       m_wids[f] == h_general || 
		       m_wids[f] == h_event || 
		       m_wids[f] == h_other ) && 
		     f+2 < m_nw &&
		     ( m_wids[f+2] == h_information || m_wids[f+2]==h_info)) 
			badFirst = true;
		// phone number field
		if ( m_wids[f] == h_phone ) badFirst = true;
		// part of address, but we do not pick it up
		if ( m_wids[f] == h_usa ) badFirst = true;
		if ( m_wids[f] == h_date ) badFirst = true;
		if ( m_wids[f] == h_description ) badFirst = true;
		// "buy tickets from $54" for events.mapchannels.com!
		if ( m_wids[f] == h_buy ) badFirst = true;
		if ( m_wids[f] == h_where ) badFirst = true;
		if ( m_wids[f] == h_location ) badFirst = true;
		if ( m_wids[f] == h_located ) badFirst = true;
		if ( m_wids[f] == h_click ) badFirst = true;
		if ( m_wids[f] == h_here ) badFirst = true;
		// "back to band profile" myspace.com
		if ( m_wids[f] == h_back && 
		     f+2 < m_nw && m_wids[f+2] == h_to )
			badFirst = true;
		// southgatehouse.com "this week"
		if ( m_wids[f] == h_this && 
		     f+2 < m_nw && m_wids[f+2] == h_week )
			skip = true;
		// "this event repeats 48 times" for 
		// http://www.zipscene.com/events/view/2848438-2-75-u-call-
		// its-with-dj-johnny-b-cincinnati was getting hammered by
		// this algo
		if ( m_wids[f] == h_this &&
		     f+2 < m_nw && m_wids[f+2] == h_event &&
		     f+4 < m_nw && m_wids[f+4] == h_repeats )
			skip = true;
		// "claim it"
		if ( m_wids[f] == h_claim &&
		     f+2 < m_nw && m_wids[f+2] == h_it )
			skip = true;
		// "claim this event"
		if ( m_wids[f] == h_claim &&
		     f+2 < m_nw && m_wids[f+2] == h_this &&
		     f+4 < m_nw && m_wids[f+4] == h_event )
			skip = true;
		// "upcoming events"
		if ( m_wids[f] == h_upcoming &&
		     f+2 < m_nw && m_wids[f+2] == h_events )
			skip = true;
		// "other upcoming events..."
		if ( m_wids[f] == h_other &&
		     f+2 < m_nw && m_wids[f+2] == h_upcoming &&
		     f+4 < m_nw && m_wids[f+4] == h_events )
			skip = true;
		// "is this your [event|venue]?"
		if ( m_wids[f] == h_is &&
		     f+2 < m_nw && m_wids[f+2] == h_this &&
		     f+4 < m_nw && m_wids[f+4] == h_your )
			skip = true;
		// "feed readers..."
		if ( m_wids[f] == h_feed &&
		     f+2 < m_nw && m_wids[f+2] == h_readers )
			skip = true;
		// "no rating..."
		if ( m_wids[f] == h_no &&
		     f+2 < m_nw && m_wids[f+2] == h_rating )
			skip = true;
		// user reviews
		if ( m_wids[f] == h_user &&
		     f+2 < m_nw && m_wids[f+2] == h_reviews )
			skip = true;
		// reviews & comments
		if ( m_wids[f] == h_reviews &&
		     f+2 < m_nw && m_wids[f+2] == h_comments )
			skip = true;
		// skip urls
		if ( (m_wids[f] == h_http || m_wids[f] == h_https) &&
		     f+6 < m_nw &&
		     m_wptrs[f+1][0]==':' &&
		     m_wptrs[f+1][1]=='/' &&
		     m_wptrs[f+1][2]=='/' ) {
			skip = true;
		}

		// single word baddies
		if ( single ) {
			if ( m_wids[f] == h_new ) {
				badFirst = true;
				// fix abqtango.com "New" as an event title
				//tscore *= .50;
			}
			// "featuring"
			if ( m_wids[f] == h_featuring ) badFirst = true;
			// "what:"
			if ( m_wids[f] == h_what ) badFirst = true;
			if ( m_wids[f] == h_who  ) badFirst = true;
			if ( m_wids[f] == h_tickets) badFirst = true;
			// a price point!
			if ( m_wids[f] == h_free ) badFirst = true;
			// navigation
			if ( m_wids[f] == h_login   ) badFirst = true;
			if ( m_wids[f] == h_back    ) badFirst = true;
			if ( m_wids[f] == h_when    ) badFirst = true;
			if ( m_wids[f] == h_contact ) badFirst = true;
			if ( m_wids[f] == h_phone   ) badFirst = true;
			// stop hours from being title
			if ( m_wids[f] == h_hours   ) badFirst = true;
			// selection menu. sort by "relevancy"
			if ( m_wids[f] == h_relevancy ) badFirst = true;
			// from southgatehouse.com
			if ( m_wids[f] == h_tonight   ) skip = true;
			if ( m_wids[f] == h_today     ) skip = true;
			if ( m_wids[f] == h_share     ) skip = true;
			if ( m_wids[f] == h_join      ) skip = true;
			if ( m_wids[f] == h_loading   ) skip = true;
			if ( m_wids[f] == h_bookmark  ) skip = true;
			if ( m_wids[f] == h_publish   ) skip = true;
			if ( m_wids[f] == h_subscribe ) skip = true;
			if ( m_wids[f] == h_save      ) skip = true;
			if ( m_wids[f] == h_creator   ) skip = true;
			if ( m_wids[f] == h_tags      ) skip = true;
			if ( m_wids[f] == h_category  ) skip = true;
			if ( m_wids[f] == h_price     ) skip = true;
			if ( m_wids[f] == h_rate      ) skip = true;
			if ( m_wids[f] == h_rates     ) skip = true;
			if ( m_wids[f] == h_users     ) skip = true;
			if ( m_wids[f] == h_support   ) skip = true;
		}


		if ( badFirst )
			si->m_sentFlags |= SENT_BAD_FIRST_WORD;

		if ( skip )
			si->m_sentFlags |= SENT_NUKE_FIRST_WORD;

	}


	Section *prevSec = NULL;
	//////////////
	//
	// now if SENT_HAS_COLON was set and the next sentence is
	// a phone number, then we can assume we are the name of a person
	// or some field for which that phone # applies
	//
	//////////////
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		//if ( si->m_minEventId <= 0 ) continue;
		// save it
		Section *lastSec = prevSec;
		// update it
		prevSec = si;
		// skip if no last yet
		if ( ! lastSec ) continue;
		// ok, last guy must have had a colon
		if ( ! (lastSec->m_sentFlags & SENT_COLON_ENDS) ) continue;

		// this is good for cabq.gov libraries page "children's room:"
		// which has a phone number after it
		if ( ! ( si->m_sentFlags & SENT_HAS_PHONE ) )
			continue;

		// ok, punish last guy in that case for having a colon
		// and preceeding a generic sentence
		//lastSec->m_titleScore *= .02;
		lastSec->m_sentFlags |= SENT_FIELD_NAME;
	}

	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	//
	// set SENT_EVENT_ENDING
	//
	// . if sentence ends in "festival" etc. set this bit
	// . or stuff like "workshop a" is ok since 'a' is a stop word
	// . or stuff like 'sunday services from 9am to 10am'
	//
	// scan the sentences
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// compute this
		int32_t alnumCount = si->m_alnumPosB - si->m_alnumPosA ;
		// set event ending/beginning
		int32_t val = hasTitleWords(si->m_sentFlags,si->m_a,si->m_b,
					 alnumCount,m_bits,m_words,true,
					 m_niceness);
		if ( val == 1 )
			si->m_sentFlags |= SENT_HASTITLEWORDS;
		if ( val == -1 )
			si->m_sentFlags |= SENT_BADEVENTSTART;
	}

	return true;
}

// returns +1 if has positive title words/phrases
// returns -1 if has negative title words/phrases
// return   0 otherwise
int32_t hasTitleWords ( sentflags_t sflags ,
		     int32_t a ,
		     int32_t b ,
		     int32_t alnumCount ,
		     Bits *bitsClass ,
		     Words *words ,
		     bool useAsterisk ,
		     int32_t niceness ) {

	// need at least two alnum words
	if ( alnumCount <= 0 ) return 0;
	// skip if too long and not capitalized
	if ( alnumCount > 7 && (sflags & SENT_MIXED_CASE ) ) return 0;

	// sanity. we need s_pit to be initialized
	if ( ! s_init9 ) initPlaceIndicatorTable();


	int64_t *wids = words->getWordIds();
	nodeid_t *tids = words->getTagIds();
	char **wptrs = words->getWords();
	int32_t *wlens = words->getWordLens();
	int32_t  nw = words->getNumWords();

	// . shortcut
	// . we are also called from dates.cpp and m_bits is NULL!
	wbit_t *bits = NULL;
	if ( bitsClass ) bits = bitsClass->m_bits;

	static bool s_flag = false;
	static int64_t h_annual;
	static int64_t h_anniversary;
	static int64_t h_next;
	static int64_t h_past;
	static int64_t h_future;
	static int64_t h_upcoming;
	static int64_t h_other;
	static int64_t h_more;
	static int64_t h_weekly;
	static int64_t h_daily;
	static int64_t h_permanent; // fix permanent exhibit for collectorsg
	static int64_t h_beginning ;
	static int64_t h_every ;
	static int64_t h_featuring ;
	static int64_t h_for ;
	static int64_t h_at ;
	static int64_t h_by ;
	static int64_t h_on ;
	static int64_t h_no ;
	static int64_t h_name ;
	static int64_t h_in ;
	static int64_t h_sponsored;
	static int64_t h_sponsered;
	static int64_t h_presented;
	static int64_t h_i;
	static int64_t h_id;
	static int64_t h_begins ;
	static int64_t h_meets ;
	static int64_t h_benefitting ;
	static int64_t h_benefiting ;
	static int64_t h_with ;
	static int64_t h_starring ;
	static int64_t h_experience;
	static int64_t h_w ;
	static int64_t h_event;
	static int64_t h_band;
	static int64_t h_tickets;
	static int64_t h_events;
	static int64_t h_jobs;
	static int64_t h_total;
	static int64_t h_times;
	static int64_t h_purchase;
	static int64_t h_look;
	static int64_t h_new;
	static int64_t h_us;
	static int64_t h_its;

	if ( ! s_flag ) {
		s_flag = true;
		h_annual = hash64n("annual");
		h_anniversary = hash64n("anniversary");
		h_next = hash64n("next");
		h_past = hash64n("past");
		h_future = hash64n("future");
		h_upcoming = hash64n("upcoming");
		h_other = hash64n("other");
		h_more = hash64n("more");
		h_weekly = hash64n("weekly");
		h_daily = hash64n("daily");
		h_permanent = hash64n("permanent");
		h_beginning = hash64n("beginning");
		h_every = hash64n("every");
		h_featuring = hash64n("featuring");
		h_for = hash64n("for");
		h_at = hash64n("at");
		h_by = hash64n("by");
		h_on = hash64n("on");
		h_no = hash64n("no");
		h_name = hash64n("name");
		h_in = hash64n("in");
		h_sponsored = hash64n("sponsored");
		h_sponsered = hash64n("sponsered");
		h_presented = hash64n("presented");
		h_i = hash64n("i");
		h_id = hash64n("id");
		h_begins = hash64n("begins");
		h_meets  = hash64n("meets");
		h_benefitting = hash64n("benefitting");
		h_benefiting = hash64n("benefiting");
		h_with = hash64n("with");
		h_starring = hash64n("starring");
		h_experience = hash64n("experience");
		h_w = hash64n("w");
		h_event = hash64n("event");
		h_band = hash64n("band");
		h_tickets = hash64n("tickets");
		h_events = hash64n("events");
		h_jobs = hash64n("jobs");
		h_total = hash64n("total");
		h_times = hash64n("times");
		h_purchase = hash64n("purchase");
		h_look = hash64n("look");
		h_new = hash64n("new");
		h_us = hash64n("us");
		h_its = hash64n("its");
	}

	// . if it just consists of one non-stop word, forget it!
	// . fixes "This series" for denver.org
	// . fixes "Practica" for abqtango.org
	// . we have to have another beefy word besides just the event ending
	int32_t goodCount = 0;
	for ( int32_t k = a ; k < b ; k++ ) {
		QUICKPOLL(niceness);
		if ( ! wids[k] ) continue;
		if ( bits && (bits[k] & D_IS_IN_DATE) ) continue;
		if ( bits && (bits[k] & D_IS_STOPWORD) ) continue;
		if ( wlens[k] == 1 ) continue;
		// treat "next" as stop word "next performance" etc.
		// to fix "next auction"?
		if ( wids[k] == h_next ) continue;
		if ( wids[k] == h_past ) continue;
		if ( wids[k] == h_future ) continue;
		if ( wids[k] == h_upcoming ) continue;
		if ( wids[k] == h_other ) continue;
		if ( wids[k] == h_more  ) continue;
		if ( wids[k] == h_beginning ) continue; // beginning on ...
		if ( wids[k] == h_weekly  ) continue; // weekly shows
		if ( wids[k] == h_daily  ) continue;
		if ( wids[k] == h_permanent ) continue;
		if ( wids[k] == h_every ) continue; // every saturday
		goodCount++;
		if ( goodCount >= 2 ) break;
	}
	// . need at least 2 non-stopwords/non-dates
	// . crap, what about "Bingo" - let reduce to 1
	//if ( goodCount <= 1 ) return 0;
	if ( goodCount <= 0 ) return 0;

	// "9th annual...."
	// "THE 9th annual..."
	// 2012 annual
	for ( int32_t k = a ; k < b ; k++ ) {
		if ( !is_digit(wptrs[k][0]))  continue;
		if ( k+2>=nw ) continue;
		if ( wids[k+2] == h_annual ) return true;
		if ( wids[k+2] == h_anniversary ) return true;
	}

	//
	// TODO: * with <person name>
	//

	// . a host list of title-y words and phrases
	// . event title indicators
	// . ^ means must be in beginning
	// . $ means must match the end
	// . * means its generic and must have 2+ words in title
	static char *s_twords[] = {
		"$jazzfest",
		"$photofest",
		"$fest", // Fan fest
		"$fanfest",
		"$brewfest",
		"$brewfestivus",
		"$musicfest",
		"$slamfest",
		"$songfest",
		"$ozfest",
		"$winefest",
		"$beerfest",
		"$winterfest",
		"$summerfest",
		"$springfest",
		"|culturefest",
		"$fest", // reggae fest
		"$fallfest",
		"$o-rama", // string-o-rama
		"$jazzshow",
		"$musicshow",
		"$songshow",
		"$wineshow",
		"$beershow",
		"$wintershow",
		"$summershow",
		"$springshow",
		"$fallshow",
		"$wintershow",
		"$winter fantasy",
		"$recital",
		"$auditions",
		"$audition",
		"$festival",
		"$the festival", // the festival of trees is held at...
		"$festivals",
		"$jubilee",
		"$concert",
		"$concerts",
		"$concerto",
		"$concertos",
		"$bout", // fight
		"*$series", // world series, concert series
		"-this series", // fix denver.org
		"-television series",
		"-tv series",
		"$hoedown",
		"$launch", // album launch
		"*$3d", // puss in boots 3d

		"*$beginning", // beginning painting constantcontact.com
		"*$intermediate", 
		"*$advanced",
		"*^beginning", // beginning painting constantcontact.com
		"-^beginning in", // beginning in january
		"-^beginning on",
		"-^beginning at",
		"*|intermediate",  // drawing, intermediate
		"*^int",
		"*^advanced",
		"*^adv",
		"*^beginner", // beginner tango
		"*$beginners", // for beginners
		"*^beg", // beg. II salsa
		"*^adult", // Adult III

		"*$con",
		"$convention",
		"$comiccon",
		"$jobfair",
		"$peepshow",
		"$ballyhoo",

		"$open", // french open
		"-$half open", // fix
		"-$we're open", // fix
		"-$is open", // fix
		"-$be open", // fix
		"-$percent open",
		"-$now open", // fix
		"-$shop open", // fix
		"-$office open",
		"-$re open",
		"-$building open", // TODO: use all place indicators!
		"-$desk open", // help desk open
		"open mic", // anywhere in the title

		// . should fix "Presented By Colorado Symphony Orchestra" from
		//   being a good event title
		"-^presented",

		"*$opening", // grand opening, art opening
		"$spectacular", // liberty belle spectacular
		"$classic", // 2nd annual cigar club classic
		"$carnival",
		"$circus",
		"$rodeo",
		"$ride", // bike ride
		"$rides", // char lift rides
		"train ride",
		"train rides",

		"$summit", // tech summit
		"$lecture",
		"*$overview", // BeachBound+Hounds+2012+Overview
		"$talk", // Curator's+Gallery+Talk
		"$discussion", // panel discussion
		"^panel discussion",
		"|public hearing",
		"$webinar",
		"$teleseminar",
		"$seminar",
		"$seminars", // S.+Burlington+Saturday+Seminars
		"$soiree", // single's day soiree
		"$extravaganza",
		"|reception",
		"-|reception desk",
		"$tribute",
		"$parade",
		"^2011 parade", // 2011 parade of homes
		"^2012 parade",
		"^2013 parade",
		"^2014 parade",
		"^2015 parade",
		"$fireworks",
		//"$car club", // social car club. "club" itself is too generic
		//"$book club",
		//"$yacht club",
		//"$glee club",
		//"$knitting club",
		//"mountaineering club",
		//"debate club",
		//"chess club",
		"*$club",
		"club of", // rotary club of kirkland
		"$forum", // The+bisexual+forum+meets+on+the+second+Tuesday
		"$meet", // swap meet
		"$swap", // solstice seed swap
		"$board", // board meeting
		"^board of", // board of selectmen
		//"-board meeting", // i'm tired of these!!
		//"-council meeting",
		"*$meeting",
		"*$mtg",
		"*$mtng",
		"*$meetings", // schedule of meetings
		"*$meet", // stonegate speech meet/ track meet
		"-no meeting",
		"-no meetings",
		"-no game",
		"-no games",
		"*$mtg", // State-Wide+Educational+Mtg
		"$meetup",
		"$meetups",
		"$meet ups",
		"$meet up",
		"*meets on" , // grade 8 meets on thursday
		"^meet the", // meet the doula
		"$committee",
		"$council", // parish council
		"$hearing", // public hearing
		"$band", // blues band
		"$quartet",
		"$trio",
		"$networking event",
		"$social", // polar express social
		"tour of", // tour of soulard / The+Chocolate+Tour+of+New+York
		"|tour",// tour boulevard brewery
		"|tours", // tours leave the main bldg...
		"$cruise",
		"$cruises",
		"cruise aboard", // dog paddle motor cruise abord the tupelo
		"motor cruise",
		"$safari", // fishing safari
		"*$trip", // photography trip
		"*$trips", // photography trip
		"$slam", // poetry slam
		"$readings", // poetry readings
		"$expedition",
		"$expeditions",
		"orchestra", // middle school orchestra in the chapel
		"$ensemble",
		"$ensembles",
		"$philharmonic",
		"$chorale",
		"$choir",
		"$chorus",
		"$prelude", // music prelude

		"-$website", // welcome to the blah website
		"-$blog",
		"-$homepage",

		"*$group", // pet loss group
		"*$groups", // 12 marin art groups
		"$sig", // special interest group
		"-$your group", // promote your group
		"-$your groups", // promote your group
		"-sub group", // IPv6+Addressing+Sub-group
		"-$a group", // not proper noun really
		"-$the group",
		"-$the groups",
		"-$by group",
		"-$by groups",
		"-$or group",
		"-$or groups",
		"-$for group",
		"-$for groups",
		"-$sub groups",
		"-$participating group",
		"-$participating groups",
		"-$large group",
		"-$large groups",
		"-$small group",
		"-$small groups",
		"-$age group",
		"-$age groups",
		"-$profit group",
		"-$profit groups",
		"-$dental group",
		"-$dental groups",
		"-$eyecare group",
		"-$eyecare groups",
		"-$medical group",
		"-$medical groups",
		"-$private group",
		"-$private groups",
		"-$media group", // locally owned by xxx media group
		"conversation group",
		"book group",
		"reading group",
		"support group",
		"support groups",
		"discussion group",
		"discussion groups",
		"$playgroup",
		"$workgroup",
		"$intergruopgroup",

		"|orientation", // Orientation+to+Floortime
		// no! "$all day" "$minimum day" from is too generic for
		// mvusd.us
		"$day", // heritage day. kite day
		"$day out", // girl's day out
		"$play day",
		"*day of", // day of action
		"*hour of", // hour of power
		"$day 2012", // local history day 2012
		"*$all day",
		"*$every day", // happy hour every day vs. "every day"
		"*$this day",
		"-$per day",
		"$caucus",
		"$caucuses",

		"*$days", // liberty lake days
		"$expo",
		"$exposition",
		"*$session",// sautrday evening jazz session
		"*$sessions",
		//"-$current exhibition", // not a good title?
		"-$current session", // fixes
		"-$current sessions",
		"-$event session",
		"-$event sessions",
		//"-$schedule",
		"-$calendar",
		"-$calendars",
		"$revue",
		"*$lesson",
		"*$lessons",
		"rehersal",
		"rehearsal", // misspelling
		"$audition",
		"$auditions",
		"*$practice",
		"*$practices", // Countryside+Christian+Basketball+Practices
		"*$practica", // common for dance
		"^guided", // guided astral travel
		"*|training", // training for leaders
		"*$exercise", // Sit+&+Fit+Chair+Exercise+for+Seniors
		"$performance",
		"$performances",
		"*$dinner",
		"*$lunch",
		"*$luncheon",
		"*$brunch",
		"$bbq",
		"$barbeque",
		"|auction", // auction begins at
		"|auctions",
		"$run", // fun run
		"$trek", // turkey trek
		"$trot", // turk trot
		"$walk", // bird walk, tunnel walk
		"$walks", // ghost walks
		"$ramble", // plymouth rock ramble
		"$crawl", // spring crawl, pub crawl
		"$ramble", // turkey ramble
		"$ceremony", // tree lighting ceremony
		"$ceremoney", // misspelling
		// "ceremonies" itself is too generic. gets 
		// "ballroom ceremonies" as part of a sales rental putch
		"$opening ceremonies", // opening ceremonies
		"art opening", // Art+Opening+-+Jeff+Hagen+Watercolorist
		"-certificate", // Coaching+Certificate
		"-$supplies", // yoga supplies
		"$awards", // 2011 Vision Awards
		"$banquet",
		"$banquets",
		"*$ball",
		"-$county", // always bad to have a county name in the title
		"-$counties",
		"scavenger hunt",
		"$celebration", // a christmas celebration
		"celebration of", // celebration of giving
		"celebrates", // +Band+Celebrates+a+Season+of+Red,...
		"celebrate", // Celebrate Recovery
		"$showdown", // sunday showdown
		"|yoga", // astha yoga
		"|meditation", // simply sitting meditation, meditation 4a calm
		"^family", // family climb, family drum time
		"$taiko", // japanese drumming
		"|karaoke", // karaoke with jimmy z
		"$party", // best of the city party
		"-$to party", // 18+ to party
		"|symposium", // event where multiple speaches made
		"|symposiums", // friday night symposiums
		"|composium",
		"|composiums",
		"|colloquium",
		"|colloquiums",
		"afterparty",
		"|blowout",
		"$potluck",
		"$pot luck",
		"$bonanza",
		"$night out", // girls' night out
		"$showcase", // bridal showcase
		"$show case", // bridal showcase
		"$sideshow", // circus sideshow
		"$hockey",
		"|bad minton",
		"|badminton",
		"ping pong",
		"pingpong",
		"$pickleball",
		"$swim", // open swim, lap swim, women only swim
		"|swimming",
		"|skiing",
		"|carving", // wood carving
		"|tai chi",
		"|balance chi",
		"|taichi",
		"|karate",
		"|judo", // kids judo
		"|wrestling",
		"|jujitsu",
		"|ju jitsu",
		"|walking",
		"|pilates",
		"|aerobics",
		"|jazzercise",
		"|birding",
		"|kickboxing",
		"|kick boxing",
		"|billiards",
		"$table tennis",
		"$basketball",
		"$fishing",
		"^fishing in", // fishing in the midwest
		"^cardiohoop",
		"$crossfit",
		"$zumba",
		"$scouts", // cub/boy/girl scouts
		"$webelos",
		"$baseball",
		"$softball",
		"$football",
		"$foosball",
		"$soccer",
		"$bb",
		"$volleyball",
		"$vb",
		"$painting", // pastel painting
		"$sculpting", // body sculpting
		"^body sculpting",
		"^chess", // chess for kids
		"$campus visit",
		"$tennis",
		"*$sale", // jewelry sale: 10% off
		"book sale",
		"book clearance",
		"$bash",
		"$pow wow",
		"$powwow",
		"$camp", // iphone boot camp / space camp
		"*bootcamp", // for one word occurences
		"*$tournament",
		"*$tournaments", // daily poker tournaments
		"$tourney",
		"$tourneys",
		"$competition",
		"$contest",
		"$cook off",
		"$bake off",
		"$kick off",
		"$fair",
		"$jam", // monday night jam
		"$jamboree",
		"$exhibition",
		"$exhibit",
		"^exhibition of",
		"^exhibit of",
		"^evening of", // evening of yoga & wine
		"group exhibition",
		"group exhibit",
		"$therapy",
		"$support", // Coshocton+County+Peer+Support
		//"^exhibition", // exhibition: .... no. "exhibit hall"
		// Graffiti Group Exhibition @ Chabot College Gallery
		"exhibition",
		"exhibitions",
		"exhibit",
		"exhibits",
		"$retrospective",
		"food drive",
		"coat drive",
		"toy drive",
		"blood drive",
		"recruitment drive",
		"waste drive",
		"donor drive",
		"$christmas",
		"$winterland",
		"$wonderland",
		"$christmasland",
		"$carol",
		"$carolling",
		"$caroling",
		"$caroles", // 3T Potluck & Caroles
		"$carols",
		"*$demo", // Spinning demo by margarete... villr.com
		"*$demonstration",
		"$debate",
		"$racing",
		"$race",
		"|5k", // 5k run/walk ... otter run 5k
		"|8k", // 5k run/walk
		"|10k", // 5k run/walk
		"$triathalon",
		"$triathlon",
		"$biathalon",
		"$biathlon",
		"$duathalon",
		"$duathlon",
		"$marathon",
		"$thon", // hack-a-thon
		"$athon",
		"$runwalk", // run/walk
		"*$relay", // Women's+Only+1/2+marathon+and+2+person+relay
		"*$hunt", // egg hunt, scavenger hunt

		// no, gets "write a review"
		//"$review", // Bone+Densitometry+Comprehensive+Exam+Review

		"|bingo",
		"|poker",
		"|billiards",
		"|bunco",
		"|crochet",
		"|pinochle",
		"|dominoes",
		"|dominos",
		"*|domino",
		"*$game", // basketball game
		"$game day", // senior game day
		"^adoption day", // Adoption+Day,+Utopia+for+Pets
		"*$gaming", // Teen+Free+Play+Gaming+at+the+Library
		"*$program", // kids program, reading program
		"school play",
		"$experience", // the pink floyd experience

		"*$programs", // children's programs
		"-^register", // register for spring programs
		"$101", // barnyard animals 101
		"*$techniques", // Core+Training+++Stretch+Techniques
		"*$technique", // course 503 raindrop technique
		"*$basics", // wilton decorating basics
		"^basics of",
		"^the basics",
		"*^basic" , // basic first aid (gets basic wire bracelet too!)
		"$first aid",
		"^fundamentals of",
		"$fundamentals", // carving fundamentals
		"^principles of",
		"$principles",
		"^intersections of",
		"$gala", // the 17th annual nyc gala
		"*$anonymous", // narcotics anonymous
		"$substance abuse", // women's substance abuse
		"$weight watchers",
		"$mass", // vietnamese mass (watch out for massachussettes)
		"midnight mass",
		"$masses",
		"|communion",
		"|keynote",
		"^opening keynote",
		"spelling bee",
		"on ice", // disney on ice
		"for charity", // shopping for charity
		"a charity", // shopping for a charity
		"storytime", // children's storytime
		"storytimes",
		"$story", // the hershey story
		"commencement", // sping commencement: undergraduate
		"$walk to", // walk to end alzheimer's
		"$walk 2", // walk to ...
		"$walk for", // walk for ...
		"$walk 4", // walk for ...
		"$walk of", // walk of hope
		"$encounters", // sea lion encounters
		"$encounter", // sea lion encounter
		"-visitor information", // Visitor+Information+@+Fort+Worth+Zoo
		"*$breakfast", // 14th+Annual+Passaic+Breakfast
		"presentation", // Annual+Banquet+&+Awards+Presentation
		"presentations",
		"-available soon",//Presentation+details+will+be+available+soon
		"bike classic",
		"$havdalah", // Children's+Donut+Making+Class+and+Havdalah
		"|shabbat",
		"|minyan", // what is this?
		"|minyans", // what is this?
		"fellowship",
		"$benefit", // The+Play+Group+Theatre+6th+Annual+Benefit
		"children's church",
		"sunday school",
		"*$event", // dog adoption event, networking event
		"-$events", // Ongoing Tango Club of Albuquerque * events at...
		"-private event",
		"-view event",
		"-view events",
		"gathering",
		"gatherings", // all-church gatherings
		"$mixer", // dallas networking mixer

		// put this into the isStoreHours() function in Dates.cpp
		//"-is open", // Our+Website+is+Open+for+Shopping+24/7
		//"-are open", // Our+Website+is+Open+for+Shopping+24/7
		//"-store hours", // Winter+Store+Hours+@+13+Creek+St.
		//"-shopping hours", // extended shopping hours
		//"-shop hours",
		//"-deadline", 
		//"-deadlines", 

		"-news", // urban planning news
		"-posted", // posted in marketing at 8pm
		"-driving directions",

		// popular titles
		"side story", // west-side story
		"westside story", // west-side story
		"doctor dolittle",
		"nutcracker",
		"mary poppins",
		"harlem globetrotters",
		"no chaser", // straight, no chaser
		"snow white",
		"charlie brown",
		"pumpkin patch",
		"marie osmond",
		"hairspray",
		"defending the", // defending the caveman
		"lion king",
		"ugly duckling",
		"santa claus", // santa claus is coming to town
		"stomp",
		"chorus line",
		"^cirque", // cirque do soleil
		"red hot", // red hot chili peppers
		"street live", // sesame street live
		"the beast", // beauty and the beast
		"lady gaga",
		"led zeppelin",
		"tom petty",
		"adam ant",
		"kid rock",
		"|annie", // little orphan annie play
		"swan lake",

		// popular event names?
		"crafty kids",
		"sit knit", // sit & knit

		// popular headliners
		//"larry king",

		// TODO: support "*ing club"(mountaineering club)(nursing club)
		// TODO: blah blah 2012 is good!!

		// gerunds (| means either $ or ^)
		"*^learning",
		"|bowling",
		"*$bowl", // orange bowl, super bowl
		"|singing", // Children's+Choirs+Singing
		"|sing along", // Messiah+Sing-Along
		"|singalong",
		"^sing", // community sing
		"$singers", // Lakeside+Singers+at+NCC
		"|soapmaking", // Girls+Spa+Day:+Lip+balm,Perfume&Soapmaking:
		"|scrapbooking", // What's+new+in+Scrapbooking
		"|exhibiting", // Exhibiting+at+The+Center+for+Photography
		"|healing", // Service+of+Healing
		"^service of", // Service+of+Remembrance,+Healing+and+Hope
		"^a healing", // a healing guide to renewing...
		"^the healing",
		"star gazing",
		"stargazing",
		"|meditating", // Meditating+With+The+Body
		"*$showing",
		"*$shooting", // Trap+shooting
		"*$skills", // Resume+and+Interviewing+Skills

		"|networking",
		"|making", // making money
		// no, was getting "serving wayne county since 1904"
		// and a bunch of others like that
		//"*^serving", // serving the children of the world
		"-serving dinner",
		"-serving breakfast",
		"-serving lunch",
		"-serving brunch",
		"|diving",
		"^hiking",
		"$hike",
		"*^varsity", // Varsity+Swim+&+Dive+-+ISL+Diving+@+Stone+Ridge
		"*^junior varsity",
		"*^jv",
		"|judging", // plant judging
		"rock climbing",
		"|kayaking",
		"|bellydancing",
		"|bellydance",
		"|belly dancing",
		"|belly dance",
		"|square dancing",
		"|square dance",
		"|swing dancing",
		"|swing dance",
		"swing night",
		"$speaking", // Pastor+Mike+speaking+-+December+18,+2011
		"|canoeing",
		"|wrestling",
		"|knitting",
		"|needlework",
		"|crocheting",
		"$voting", // early voting
		"|printmaking",
		"|making", // paper bead making
		"|writing", // ENG+2201:+Intermediate+College+Writing
		"|sharing", // Wisdom+of+the+Sangha:+Sharing,+Reflection...
		"|decorating", // wilton decorating basics
		"|reading", // Tess+Gallagher+reading+at+Village+Books
		"|readings", // Tess+Gallagher+reading+at+Village+Books
		"-currently reading", // Currently Reading:
		"|poetry",
		"$and friends",
		"*^celebrating", // Celebrating+Sankrant+2012
		"*^interpreting", // intepreting for the deaf
		"*^researching", // "Researching+and+Reflecting:+Sounds,+Sights
		"*^reflections", // Stories:+Reflections+of+a+Civil+War+Historia
		"*^reflecting",
		"*^enhancing",
		"*^mechanisms", // Mechanisms+Involved+in+Generating...
		"*^finding", // finding+time+to...
		"*^transforming", // Transforming+Transportation+2012
		"*^reinventing",
		"*^making", // Making+Good+on+the+Promise:+Effective+Board+...
		"*^creating", // Creating+Cofident+Caregivers
		"*^giving", // Giving+in+ALLAH's+Name
		"*^getting", // Getting+Out+of+Debt, Getting+to+Know...
		"-getting here", // directions! bad!
		"-getting there",// directions! bad!
		"*^turning", // Turning+Your+Strategic+Plan+into+an+Action+Plan
		"*^engaging", // Engaging+Volunteers+with+Disabilties
		"*^governing",
		"*^managing", // Managing+Your+Citations+with+RefWorks
		"*^entering", // entering the gates of judaism
		"*^stregthening", // ...+Baptist+Church:+Strengthening+the+Core
		"treeplanting",
		"-managing director",
		"-managing partner",
		"^issues in",
		"^the issues",
		"*^countdown", // countdown to new year's eve sweepstakes
		"*^navigating", // Navigating+the+Current+Volatility+
		// defensive driving
		"*|driving", // Driving+Innovation+&+Entrepreneurship
		"*^using", // Using+OverDrive
		"*^letting", // Letting+Poetry+Express+Your+Grief
		"*^feeding", // Feeding+Children+Everywhere
		"*^feeling", // Feeling+Out+of+Balance?
		"*^educating", // "Educating+for+Eternity"
		"*^demystifying", // like understanding (demystifying weightloss
		"*^discovering",
		"*^equipping", // Equipping+Ministry+-+Gospel+and+Culture
		//"-^no", // no mass, no ... HURTS "no name club" too bad!!
		"-$break", // fall break, winter break...
		"-not be", // there will not be a dance in december
		"-pollution advisory",
		"-|closed", // holiday hours - museum closed, closed for xmas
		"-is closing",
		"-is closed",
		"-be closing",
		"-be closed",
		"-are closing",
		"-are closed",
		"-$vacation", // vacation
		"-cancelled",
		"-canceled",
		"-^calendar", // calendar of events
		"-^view", // view upcoming events inlyons
		"-^suggest", // suggest a new event
		"-^find us", // find us on facebook...
		"-^hosted by", // Hosted by Cross Country Education (CCE)
		"*^comment", // Comment+in+George+Test+Group+group
		"-^purchase", // Purchase Products from this Seminar
		"-in recess", // class in recess
		"*^playing", // Playing+For+Change
		"*$drawing", // portrait drawing
		"*^integrating",
		"*^greening",
		"*^dispelling",
		"*^growing", // Growing+Up+Adopted
		"*^looking",
		"*^communicating",
		"*^leasing",
		"*^assessing",
		"^quit smoking",
		"*^exploring",
		"history of", // "Headgear:+The+Natural+History+of+Horns+and+
		"*^are your", // Are+Your+Hormones+Making+You+Sick?
		"*^are you",
		"*^when will", // When+Will+the+Outdoor+Warning+Sirens+Sound?
		"*^concerned about", // Concerned+About+Outliving+Your+Income?
		"*discover your", // discover your inner feelings
		"*creating your", // creating your personal brand
		"walking tour",
		"|strategies", // speach topics
		"*|coaching", // strategies for coaching
		"*|watching", // watching paint dry
		"ice skating",
		"free screening",
		"film screening",
		"^screening of",
		"^the screening",
		"movie screening",
		"$trilogy", // back to the future trilogy
		"dj spinning",
		"$screening", // Free+Memory+Screening
		"$screenings", // Salt+Ghost+DVD+screenings+at+9+p.m
		"$tastings", // Chocolate+Tastings+at+Oliver+Kita+Chocolates
		"wine bar",
		"open bar",
		"open gym",
		"open swim",
		"*byob", // always a good indicator
		"tea tastings",
		"*coming to", // santa claus is coming to town, blah blah...

		"^improv", // improv and standup
		"^standup",
		
		"*$circle", // past life healing circle
		"^circle of", // circle of knitting (not "circle 8's" sq dnc)
		"$invitational",
		"$invitationals",
		"-$the market", // new on the market (house sale)
		"*$market", // villr.com Los Ranchos Growers' Market
		"*$markets",// villr.com fix for "Markets ((subsent))"
		"$bazaar", // holiday bazaar
		"$bazzaar", // holiday bazaar
		"$sailing", // boat sailing
		"*$sail", // free public sail
		"candle lighting",
		"menorah lighting",
		"*$lights", // river of lights, holiday lights
		"*$lighting",
		"tree lighting",
		"tree trimming",
		"book signing",
		"booksigning",
		"bookfair",
		"ribbon cutting",
		"*special guest", // +&+Hits+with+special+guest+Billy+Dean
		// got Myspace.com:Music instead of the better title
		// for music.myspace.com
		//"$music", // live music
		"^music at", // music at the mission
		"$of music",
		//"$of jazz", // evening of jazz, jazz music
		"|jazz", // children's jazz
		"$feast", // a bountiful burlesque feast
		//"$live", // junkyard jane live - but gets a place name "Kobo live".. so bad
		"*$spree", // let it snow holiday shopping spree

		"|public skating",
		"^public skate",

		// no caused 'state emission inspection' to come up
		//"$inspection", // building life safety inspections
		//"$inspections",

		"*^occupy", // occupy portland
		"$fundraiser",
		"^fundraising", // fundraising performance for ...
		"$raffle",
		"$giveaway", // anniversary quilt giveaway
		"$townhall",
		"town hall",
		"open house",
		"open houses", // then a list of times...
		"pumpkin patch",
		"happy hour",
		"cook off",
		"bake off",
		"story time",
		"story telling",
		"storytelling",
		"story hour",
		"speed dating",

		// this can be a place name too easily: the empty bottle
		// The Florida Rowing Center
		//"^the", // IS THIS GOOD???? not for "

		"$worship",
		"$rosary",
		"bible study",
		"bible studies",
		"torah study",
		"torah studies",
		"$prayer", // bible study and prayer
		"^pray", // pray in the new year
		"$eve service", // christmas/thanksgiving eve service
		"$penance service",
		"$penance services",
		"$candlelight service",
		"$candlelight", // carols & candlelight
		"eve worship",
		"eucharist",
		"worship service",
		"worship services",
		"morning service",
		"morning services",
		"night service",
		"night services",
		"evening service",
		"evening services",
		"sunday services", // church stuff
		"monday services", // church stuff
		"tuesday services", // church stuff
		"wednesday services", // church stuff
		"thursday services", // church stuff
		"friday services", // church stuff
		"saturday services", // church stuff
		"worship services", // church stuff
		"church services", // church stuff
		"sunday service", // church stuff
		"monday service", // church stuff
		"tuesday service", // church stuff
		"wednesday service", // church stuff
		"thursday service", // church stuff
		"friday service", // church stuff
		"saturday service", // church stuff
		"day service", // memorial day service, etc.
		"candleight service",
		"prayer service",
		"traditional service",
		"traditions service",
		"blended service",
		"shabbat service",
		"shabbat services",
		"contemporary service",
		"lenten service",
		"celebration service",
		"worship service",
		"eucharist service",
		"service times",
		"service time",
		"sunday mass",
		"monday mass",
		"tuesday mass",
		"wednesday mass",
		"thursday mass",
		"friday mass",
		"saturday mass",

		"$taco tuesdays",

		"$tasting", // wine tasting
		"$tastings", // wine tastings
		"*$conference", // Parent/Teacher+Conference+
		"*$conferences",
		"*$retreat",
		"$adventure", // big bird's adventure
		"^the adventures", // the adventures of...
		"$workshop", // "$workshop a" for aliconferences.com
		"$workshops", // budget workshops
		"$worksession", // city council worksession
		"^rally", // rall to support the EPA's mercury health standards
		"$rally",  // pep rally
		"$fair",
		"$fairs",
		"*$night", //biker night for guysndollsllc.com,family fun night
		// dance club nights - hurts ritmo caliente because we lose
		// that title and get "$main room, dance nights, 21+"
		//"$nights", 
		"*^dj", // dj frosty
		"$music nights",
		"$championships",
		"$championship",
		"$challenge",
		"$picnic",
		"$dance",
		"$dancing", // irish traditional dancing
		"$dancers", // irish ceili dancers
		"$freestyle", // type of dance
		"^freestyle", // type of dance
		"^cardio",
		"*$fitness", // pre-natal fitness
		"*$workout", // football workout
		"-school of", // nix school of ballet
		"$tango",  // tango for beginners
		"$ballet", 
		"$preballet", 
		"|linedancing",
		"$modern",
		"$waltz", 
		"$polka", 
		"$musical", // menopause the musical
		"$swing",
		"$milonga", // type of dance
		"$bachata",
		"|salsa", // Salsa in New Mexico
		"$tap",
		"$pagent",
		"$pageant", // christmas pageant
		"*$tutoring",
		"*$tutorial",
		"*$tutorials",
		"*$instruction",
		"*$education", // childbirth education
		"-no class", // holday break - no classes
		"-no classes",// holday break - no classes
		"-unavailable",
		"-last class",
		"-no school",
		"*$class",
		"*$classes",
		"*$teleclass",
		"*$certification",
		"*$class week", // massage class week 1
		"*$class level", // massage class level 1
		"*$mixed level", // Hatha+Basics+Mixed+Level
		"*$mixed levels",
		"*$part 1", // Recent+Acquisitions,+Part+I:+Contemporary+Photos
		"*$part 2",
		"*$part 3",
		"*$part 4",
		"*$part i",
		"*$part ii",
		"*$part iii",
		"*$part iv",
		"*$class session", // massage class session 1
		"*$course",
		"*$courses",
		"-$golf course",
		"-$golf courses",
		"*$lessons", // Free Latin Dancing Lessons
		"*$lesson",
		"-$no shows", // we bill for no shows
		"*$show", // steve chuke show
		"*$shows",
		"-no show",
		"-no shows",
		"-past shows",
		"-future shows",

		// stuff at start
		"*^annual",
		"|anniversary", // anniversary dinner, 2 year anniversary
		"festival of", // Masonicare+Festival+of+Trees
		"^learn to",
		"*^understanding", // Understanding Heart Valves (lecture)
		"*introduction to", // lecture
		"*^introductory", // introductory potter's wheel
		"^introduction", // ... : introduction climb
		"*how to", // cheap how to succeed in business...
		"-reach us", // how to reach us
		"-contact us", // how to contact us
		"*intro to", // lecture
		"*^the story", // the story of carl kiger
		"*^all about", // Class - All About Sleep Apnea
		"*$an introduction", // lecture
		"*$an intro", // lecture
		"*^the wonder", // the wonder of kites
		//"^dance", // 'dance location map' ceder.net!
		"^graduation",
		"*$special", // valentine's special
		"*world premier",

		// stuff in the middle
		"*vs",
		"*versus",
		"*class with",
		"*class w", // class w/
		"*class at",
		"*class on",
		"*class in", // class in downtown palo alto
		"*classes with",
		"*classes w", // class w/
		"*classes at",
		"*classes on",
		"-^half day", // half day of classes
		"*$on display", // trains on display
		"*show with",
		"*dance with",
		"*dancing with",
		"^free dance", // free dance fusion class at ...
		"*dance at",
		"dancing lessons",
		"*art of", // the art of bill smith
		"*^art at", // art at the airport
		"*meeting at",
		"*meetings are", // meetings are from 7pm to 9pm
		"*$and greet", // meet and greet
		"$meet greet", // meet & greet
		"*$and mingle", // mix and mingle
		"*free lessons",
		"*lessons at", // free dancing lessons at
		"*dance for" // dance for the stars
	};

	// store these words into table
	static HashTableX s_twt;
	static char s_twtbuf[4000];
	static bool s_init10 = false;
	if ( ! s_init10 ) {
		s_init10 = true;
		s_twt.set(8,4,128,s_twtbuf,4000,false,0,"ti1tab");
		int32_t n = (int32_t)sizeof(s_twords)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_twords[i];
			// set words
			Words w;
			w.set ( s, true, 0 );

			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ ) {
				// fix "art of" = "of art"
				if (! wi[j] ) continue;
				h <<= 1LL;
				h ^=  wi[j];
			}
			// wtf?
			if ( h == 0LL ) { char *xx=NULL;*xx=0; }
			// . store hash of all words, value is ptr to it
			// . put all exact matches into sw1 and the substring
			//   matches into sw2
			if ( ! s_twt.addKey ( &h , &s ) ) return false;
		}
	}

	// store these words into table
	if ( ! s_init3 ) initGenericTable ( niceness );

	// scan words in [a,b) for a match. 
	// skip if in date or stop word

	// ok, now scan forward. the word can also be next to
	// strange punct like a colon like 
	// "Hebrew Conversation Class: Beginning" for dailylobo.com
	int32_t i = a;
	int64_t firstWid = 0LL;
	int64_t lastWid  = 0LL;
	bool hadAnnual = false;
	bool hadFeaturing = false;
	bool lastWordWasDate = false;
	bool negMatch = false;
	bool posMatch = false;
	bool oneWordMatch = false;
	bool hadAthon = false;
	for ( ; i < b ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		// first word?
		if ( ! firstWid ) firstWid = wids[i];
		// does it match a word in the table?
		char **sp = (char **)s_twt.getValue ( &wids[i] );
		// a match?
		if ( sp ) oneWordMatch = true;
		// or a two word phrase? even if we matched a one word
		// phrase, try for the two because it might be a negative!
		// i.e. "half open"
		if ( lastWid ) {
			int64_t combo = wids[i] ^ (lastWid<<1LL);
			char **sp2 = (char **)s_twt.getValue ( &combo );
			// if there use that! otherwise, leave sp alone
			if ( sp2 ) sp = sp2;
			if ( sp2 ) oneWordMatch = false;
		}
		// get next wid after us
		int64_t nextWid = 0LL;
		for ( int32_t k = i + 1 ; k < b ; k++ ) {
			QUICKPOLL(niceness);
			if ( ! wids[k] )  continue;
			nextWid = wids[k];
			break;
		}
		// "-getting there" to prevent "getting" from winning
		if ( nextWid ) {
			int64_t combo = (wids[i]<<1LL) ^ nextWid;
			char **sp2 = (char **)s_twt.getValue ( &combo );
			// if there use that! otherwise, leave sp alone
			if ( sp2 ) sp = sp2;
		}

		if ( wids[i] == h_annual ) hadAnnual = true;
		// must not be the first word or last word
		if ( wids[i] == h_featuring && i > a && i < b-1 ) 
			hadFeaturing = true;

		// any kind of athon, hackathon, etc. sitathon
		if ( wlens[i]>=8 &&
		     to_lower_a(wptrs[i][wlens[i]-5]) == 'a' &&
		     to_lower_a(wptrs[i][wlens[i]-4]) == 't' &&
		     to_lower_a(wptrs[i][wlens[i]-3]) == 'h' &&
		     to_lower_a(wptrs[i][wlens[i]-2]) == 'o' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 'n' )
			hadAthon = true;

		// any *fest too!! assfest
		if ( wlens[i]>=7 &&
		     to_lower_a(wptrs[i][wlens[i]-4]) == 'f' &&
		     to_lower_a(wptrs[i][wlens[i]-3]) == 'e' &&
		     to_lower_a(wptrs[i][wlens[i]-2]) == 's' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 't' )
			hadAthon = true;

		// save it
		int64_t savedWid = lastWid;
		// assign
		lastWid = wids[i];
		// save this
		bool savedLastWordWasDate = lastWordWasDate;
		// and update
		lastWordWasDate = (bool)(bits && (bits[i] & D_IS_IN_DATE));
		// match?
		if ( ! sp ) continue;
		// get the char ptr
		char *pp = *sp;
		// or total like "total courses: xx. total sections: yy"
		if ( savedWid == h_total ) return -1;
		// . if prev word was "no" then return -1
		// . fix "no mass" "no class" etc.
		// . oneWordMatch fixes "Straight No Chaser" play title
		//   which has "no chaser" in the list above
		if ( savedWid == h_no && oneWordMatch ) return -1;
		// fix "past conferences"
		if ( savedWid == h_past && oneWordMatch ) return -1;
		// aynthing starting with no is generally bad...
		// like "no class" "no service", etc.
		if ( savedWid == h_no && firstWid == h_no &&
		     // UNLESS it's "no name" - like "the no name club"
		     lastWid != h_name ) 
			return -1;
		// return value is true by default
		bool *matchp = &posMatch;
		// . is it generic? "sesssions", etc. that means we need
		//   2+ alnum words, we can't have a title that is just 
		//   "sessions"
		// . if it is generic and we only have one word, forget it!
		if ( *pp == '*' && alnumCount == 1 && useAsterisk ) 
			continue;
		// skip asterisk
		if ( *pp == '*' ) 
			pp++;
		// is it negative. complement return value then
		if ( *pp == '-' ) { matchp = &negMatch; pp++; }
		// anywhere?
		if ( *pp != '$' && *pp != '^' && *pp != '|' ) {
			*matchp = 1;
			continue;
		}
		// the gerunds mismatch easily and therefore we require for
		// the match to be complete that we not be mixed case. fixes
		// "Looking forward to seeing you again"
		int32_t pplen = gbstrlen(pp);
		if ( pp[pplen-1]=='g' &&
		     pp[pplen-2]=='n' &&
		     pp[pplen-3]=='i' &&
		     ( sflags & SENT_MIXED_CASE ) )
			continue;
		// yes! must match first part of sentence
		if ( (*pp == '^' || *pp == '|' ) &&
		     ( wids[i] == firstWid ||
		       lastWid   == firstWid ) )
			*matchp = 1;
		// . or if a colon was right before us...
		// . Hacking+the+Planet:+Demystifying+the+Hacker+Space
		if ( (*pp == '^' || *pp == '|' ) && 
		     i>a &&
		     words->hasChar(i-1,':') )
			*matchp = 1;
		// or date right before us
		if ( (*pp == '^' || *pp == '|' ) && savedLastWordWasDate )
			*matchp = 1;
		// . this is always good (HACK)
		// . annual party / annual spring concert/annual nye afterparty
		if ( hadAnnual && *pp != '-' ) *matchp = 1;
		// keep chugging if must match first word in sentence
		if ( *pp == '^' ) continue;
		// stop if end of the line, that counts as well
		if ( i + 2 >= b ) { *matchp = 1; continue; }
		// tags are good
		if ( tids[i+1] ) *matchp = 1;
		// fix "...dance class,</strong>"
		if ( tids[i+2] ) *matchp = 1;
		// fix "workshop a" for aliconferences.com
		if ( i == b - 3 && wlens[b-1] == 1 )
			*matchp = 1;
		// Dance III..., Adult II...
		if ( i+2 < b && wptrs[i+2][0]=='I'&&wptrs[i+2][1] == 'I')
			*matchp = 1;
		// Dance I ...
		if ( i+2 < b && wlens[i+2]==1 && wptrs[i+2][0] == 'I' )
			*matchp = 1;
		// Ballet V ...
		if ( i+2 < b && wlens[i+2]==1 && wptrs[i+2][0] == 'V' )
			*matchp = 1;
		// Ballet VI ...
		if ( i+2 < b && wptrs[i+2][0] == 'V'&&wptrs[i+2][1]=='I')
			*matchp = 1;
		// hitting a date is ok... (TODO: test "... every saturday"
		if ( bits && (bits[i+2] & D_IS_IN_DATE) ) *matchp = 1;
		// a lot of times it does not treat the year as a date!
		// "Radio City Christmas Spectacular 2011"
		if ( i+2 < b && 
		     is_digit(wptrs[i+2][0]) &&
		     wlens[i+2]==4 &&
		     words->getAsLong(i+2) >= 2005 &&
		     words->getAsLong(i+2) <= 2040 )
			*matchp = 1;
		// the word for is ok? "training for grades ..."
		if ( wids[i+2] == h_for ) *matchp = 1;
		// how about "of"... symposium of science. celebration of ...
		//if ( wids[i+2] == h_of ) *matchp = 1;		
		// at following is ok. Tess+Gallagher+reading+at+Village+Book
		if ( wids[i+2] == h_at ) *matchp = 1;
		// music by the bay
		if ( wids[i+2] == h_by ) *matchp = 1;

		// "in" can be a delimeter if a place name follows it like
		// "the conference room" or "the Martin Building" ...
		// or "Zipperdome, 123 main street"
		// fix subsent:
		// ... Pre-Hearing Discussion in the Conference Room...
		// for legals.abqjournal.com/legals/show/273616
		// TODO: a city/state!!!
		if ( wids[i+2] == h_in ) {
			*matchp = 1;
		}

		// put in subsent code 
		//if ( i+4<b && wids[i+2] == h_directed && wids[i+4] == h_by ) 
		//	*matchp = 1;
		// Mass on January 1
		if ( i+4<b && wids[i+2]==h_on && bits &&
		     (bits[i+4]&D_IS_IN_DATE) )
		     *matchp = 1;		
		// blah blah in the chapel / in the Park / symposium in Boise..
		if ( i+6<b && wids[i+2] == h_in )
			*matchp = 1;
		// beginning at
		if ( i+4<b && wids[i+2]==h_beginning && wids[i+4]==h_at )
			*matchp = 1;
		// begins at
		if ( i+4<b && wids[i+2]==h_begins && wids[i+4]==h_at )
			*matchp = 1;
		// club meets at
		if ( i+4<b && wids[i+2]==h_meets && wids[i+4]==h_at )
			*matchp = 1;
		// rehersal times, concert times, blah blah times
		if ( i+3 == b && wids[i+2]==h_times )
			*matchp = 1;
		// blah blah benefitting blah blah
		if ( wids[i+2] == h_benefitting ) *matchp = 1;
		if ( wids[i+2] == h_benefiting  ) *matchp = 1;
		// blah blah party sponsored by ...
		if ( i+4<b && wids[i+2] == h_sponsored ) *matchp = 1;
		if ( i+4<b && wids[i+2] == h_sponsered ) *matchp = 1;
		// blah blah party presented by ...
		if ( i+4<b && wids[i+2] == h_presented ) *matchp = 1;
		// a colon is good "Soapmaking: how to"
		if ( words->hasChar (i+1,':' ) && 
		     // watch out for field names
		     wids[i] != h_event &&
		     wids[i] != h_band )
			*matchp = 1;
		// likewise a hyphen "Class - All About Sleep Apnea"
		if ( words->hasChar (i+1,'-' ) && 
		     // watch out for field names
		     wids[i] != h_event &&
		     wids[i] != h_band )
			*matchp = 1;
		// or parens: Million+Dollar+Quartet+(Touring)
		if ( words->hasChar (i+1,'(' ) ) *matchp = 1;
	}

	// return it if we got something
	if ( negMatch ) return -1;
	if ( posMatch ) return  1;
	if ( hadAthon ) return  1;

	// blah blah featuring blah blah
	if ( hadFeaturing ) return 1;// true;

	// . if it has quotes, with, at, @, *ing, "w/" set it
	// . if has mixed case do not do this loop
	if ( sflags & SENT_MIXED_CASE ) b = 0;
	int32_t hadNonDateWord = 0;
	int64_t lastWordId = 0LL;
	bool lastWordPastTense = false;
	// loop over all words in the title
	for ( i = a ; i < b ; i++ ) {
		QUICKPOLL(niceness);
		if ( tids[i] ) continue;
		// check for puncutation based title indicators
		if ( ! wids[i] ) {
			// MCL+Meet+@+Trego
			if ( words->hasChar(i,'@' ) &&
			     // fix "Sunday Oct 4 2pm at ..." for zvents.com
			     hadNonDateWord &&
			     // last word is not "tickets"!! fix zvents.com
			     lastWordId != h_tickets &&
			     // only for place names not tods like "@ 2pm"
			     i+1<b && ! is_digit(words->m_words[i+1][0]) )
				break;
			// "Chicago"
			if ( i>0 && 
			     ! tids[i-1] &&
			     ! wids[i-1] &&
			     words->hasChar(i-1,'\"') )
				break;
			// Event:+'Sign+Language+Interpreted+Mass'
			if ( i>0 && 
			     ! tids[i-1] &&
			     ! wids[i-1] &&
			     words->hasChar(i,'\'') ) 
				break;
			continue;
		}

		// blah blah with Tom Smith
		if ( i > a && 
		     i+2 < b &&
		     (wids[i] == h_with || wids[i] == h_starring) &&
		     // with id doesn't count
		     wids[i+2] != h_i &&
		     wids[i+2] != h_id &&
		     // experience with quickbooks
		     (i-2<a || wids[i-2] != h_experience) &&
		     // with purchase
		     wids[i+2] != h_purchase &&
		     // with $75 purchase
		     (i+4>=b || wids[i+4] != h_purchase) )
			break;
		// blah blah w/ Tom Smith
		if ( i > a && 
		     i+2 < b &&
		     wids[i] == h_w && 
		     i+1<b && 
		     words->hasChar(i+1,'/') &&
		     // with id doesn't count
		     wids[i+2] != h_i &&
		     wids[i+2] != h_id )
			break;
		// "Lotsa Fun at McCarthy's"
		if ( wids[i] == h_at &&
		     // fix "Sunday Oct 4 2pm at ..." for zvents.com
		     hadNonDateWord &&
		     // last word is not "tickets"!! fix zvents.com
		     lastWordId != h_tickets &&
		     // not look at.. take a look at the menu
		     lastWordId != h_look && 
		     // what's new at the farm
		     lastWordId != h_new &&
		     // fix "Events at Stone Brewing Co"
		     lastWordId != h_events &&
		     // search jobs at aria
		     lastWordId != h_jobs &&
		     // write us at ...
		     lastWordId != h_us &&
		     // at its best
		     (i+2>=b || wids[i+2] != h_its) &&
		     // she studied at the rcm
		     ! lastWordPastTense &&
		     // . lastword can't be a place indicator
		     // . Aquarium at Albuquerque ...
		     // . Museums at Harvard
		     ! s_pit.isInTable(&lastWordId) &&
		     // only for place names not tods like "at 2pm"
		     i+2<b && ! is_digit(words->m_words[i+2][0]) )
			break;

		bool isDateWord = (bool)(bits && (bits[i] & D_IS_IN_DATE));
		// mark this
		if ( ! isDateWord ) hadNonDateWord++;
		// save this
		lastWordId = wids[i];

		// assume not past tense
		lastWordPastTense = false;
		// is it past tense like "studied"?
		int32_t wlen = wlens[i];
		if ( to_lower_a(wptrs[i][wlen-1]) != 'd' ) continue;
		if ( to_lower_a(wptrs[i][wlen-2]) != 'e' ) continue;
		// exceptions: feed need
		if ( to_lower_a(wptrs[i][wlen-3]) == 'e' ) continue;
		// exceptions: ned zed bed
		if ( wlen == 3 ) continue;
		// probably a ton more exceptions must let's see how it does
		lastWordPastTense = true;
	}
	// i guess we had something nice...
	if ( i < b ) return 1; // true;

	// no match
	return 0;
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

// . returns -1 if already got score for the suggested delimeter
// . returns -2 if suggested section cannot be a delimeter for the
//   suggested method
// . returns -3 with g_errno set on error
// . returns  0 if no viable partition exists which has a domdow/tod pair
//   in one interval of the partition and a domdow/tod pair in another
//   interval of the partition
// . otherwise returns a postive score of the strength of the partition
// . assumes all sections with the same "getDelimHash() as "delim" are the
//   first section in a particular partition cell
int32_t Sections::getDelimScore ( Section *bro , 
			       char method , 
			       Section *delim ,
			       Partition *part ) {

	// save it
	Section *start = bro;

	int32_t dh = getDelimHash ( method , delim );

	// bro must be certain type for some methods
	if ( dh == -1 ) return -2;

	// did we already do this dh?
	if ( m_ct.isInTable ( &dh ) ) return -1;

	// ignore brothers that are one of these tagids
	sec_t badFlags = SEC_SELECT|SEC_SCRIPT|SEC_STYLE|SEC_HIDDEN;

	// get the containing section
	Section *container = bro->m_parent;
	// sanity check... should all be brothers (same parent)
	if ( delim->m_parent != container ) { char *xx=NULL;*xx=0; }

	// scores
	int32_t brosWithWords = 0;
	int32_t maxBrosWithWords = 0;
	int32_t bonus1    = 0;
	int32_t bonus2    = 0;
	int32_t bonus3    = 0;
#define MAX_COMPONENTS 15000
	int32_t pva[MAX_COMPONENTS];
	int32_t pvb[MAX_COMPONENTS];
	int32_t sva[MAX_COMPONENTS];
	int32_t svb[MAX_COMPONENTS];
	int32_t  nva  = 0;
	int32_t  nvb  = 0;
	pva[0] = 0;
	pvb[0] = 0;
	int32_t *pvec    = NULL;
	int32_t *pnum    = NULL;
	int32_t *pscore  = NULL;
	bool  firstDelim = true;
	float simTotal = 0;
	float minSim = 101.0;
	int32_t mina1 = -2;
	int32_t mina2 = -2;
	int32_t minTotalComponents=0;
	Section *prevPrevStart = NULL;
	Section *prevStart = NULL;
	int32_t  simCount = 0;
	int32_t inserts = 0;
	int32_t skips = 0;

	// no longer allow dups, keep a count of each hash now
	char vhtbuf[92000];
	HashTableX vht;
	vht.set ( 4, 4 ,256,vhtbuf,92000,false,m_niceness,"vhttab");
	int32_t cellCount = 0;
	SafeBuf minBuf;

	HashTableX labels;
	labels.set ( 4,65,20000,NULL,0,false,m_niceness,"lbldbug");
	HashTableX *dbt = NULL;
	SafeBuf sb;
	SafeBuf *pbuf = NULL;

	//
	// assign for debug DEBUG DEBUG implied sections
	//
	//dbt = &labels;
	//pbuf = &sb;

	int32_t np = 0;
	int32_t nonDelims = 0;

	bool ignoreAbove = true;

	// reset prev sentence
	Section *prevSent = NULL;

	// scan the brothers
	for ( ; ; bro = bro->m_nextBrother ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if bad
		if ( bro && (bro->m_flags & badFlags) ) continue;

		// if any brother is an implied section, stop!
		if ( bro && (bro->m_baseHash == BH_IMPLIED ) ) return -2;

		// get its hash
		int32_t h = 0LL ;
		if ( bro ) h = getDelimHash ( method , bro );

		// . check this out
		// . don't return 0 because we make a vector of these hashes
		//   and computeSimilarity() assumes vectors are NULL term'd
		if ( bro && h == 0 ) { char *xx=NULL;*xx=0; }

		// once we hit the delimeter we stop ignoring
		if ( h == dh ) ignoreAbove = false;

		// if first time, ignore crap above the first delimeter occurnc
		if ( ignoreAbove ) continue;

		// count non delimeter sections. at least one section
		// must have text and not be a delimeter section
		if ( h != dh && bro && bro->m_firstWordPos >= 0 )
			nonDelims++;

		// start a new partition?
		if ( h == dh ) {
			// start new one
			part->m_a        [np] = bro->m_a;
			part->m_b        [np] = bro->m_b;
			part->m_firstBro [np] = bro;
			np++;
			part->m_np = np;
			// if out of buffer space, note it and just do not
			// do this partition
			if ( np >= MAXCELLS ) {
				log("sec: partition too big!!!");
				return -2;
			}
		}
		// always extend current partition
		else if ( np > 0 && bro ) {
			part->m_b[np-1] = bro->m_b;
		}

			
		// did we finalize a cell in the paritition?
		bool getSimilarity = false;
		// if we hit a delimiting brothers, calculate the similarity
		// of the previous brothers
		if ( h == dh ) getSimilarity = true;
		// if we end the list of brothers...
		if ( ! bro   ) getSimilarity = true;
		// empty partition?
		if ( vht.isTableEmpty() ) getSimilarity = false;

		// convert our hashtable into a vector and compare to
		// vector of previous parition cell if we hit a delimeter
		// section or have overrun the list (bro == NULL)
		if ( getSimilarity ) {

			// if we have been hashing sentences in the previous
			// brothers, then hash the last sentence as a previous
			// sentence to a NULL sentence after it.
			// as a kind of boundary thing. i.e. "*last* sentence 
			// is in header tag, etc." just like how we hash the 
			// first sentence with "NULL" as the previous sentence.
			if (!hashSentBits(NULL,&vht,container,0,dbt,NULL))
				return -3;

			if (!hashSentPairs(prevSent,NULL,&vht,container,dbt))
				return -3;

			// reset this since it is talking about sentences
			// just in the partition cell
			prevSent = NULL;
			// inc this for flip flopping which vector we use
			cellCount++;
			// what vector was used last?
			if ( (cellCount & 0x01) == 0x00 ) {
				pvec    = pvb;
				pnum    = &nvb;
				pscore  = svb;
			}
			else {
				pvec    = pva;
				pnum    = &nva;
				pscore  = sva;
			}
			// reset vector size
			*pnum = 0;
			// convert vht to vector
			for ( int32_t i = 0 ; i < vht.m_numSlots ; i++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip if empty
				if ( ! vht.m_flags[i] ) continue;
				// add it otherwise
				pvec[*pnum] = *(int32_t *)(&vht.m_keys[i*4]);
				// add score
				pscore[*pnum] = *(int32_t *)(&vht.m_vals[i*4]);
				// sanity
				if ( pscore[*pnum] <= 0){char *xx=NULL;*xx=0;}
				// inc component count
				*pnum = *pnum + 1;
				// how is this?
				if(*pnum>=MAX_COMPONENTS){char *xx=NULL;*xx=0;}
			}
			// null temrinate
			pvec[*pnum] = 0;
			// . this func is defined in XmlDoc.cpp
			// . vec0 is last partitions vector of delim hashes
			// . this allows us to see if each section in our
			//   partition consists of the same sequence of
			//   section "types" 
			// . TODO: just compare pva to pvb and vice versa and
			//   then take the best one of those two compares!
			//   that way one can be a SUBSET of the other and
			//   we can get a 100% "sim"
			float sim = computeSimilarity2( pva  ,
							pvb  ,
							sva  , // scores
							svb , // scores
							m_niceness ,
							pbuf ,
							dbt ,
							nva );
			// add up all sims
			if ( cellCount >= 2 ) { // ! firstTime ) {
				simTotal += sim;
				simCount++;
			}
			if ( cellCount >= 2 && sim < minSim ) {
				minSim = sim;
				if ( prevPrevStart ) mina1=prevPrevStart->m_a;
				else                 mina1  = -1;
				if ( prevStart ) mina2  = prevStart->m_a;
				else             mina2  = -1;
				minTotalComponents = nva + nvb;
				// copy to our buf then
				if ( pbuf ) 
					minBuf.safeMemcpy ( pbuf );
			}
			// reset vht for next partition cell to call
			// hashSentenceBits() into
			vht.clear();
		}

		if      ( h == dh && brosWithWords >= 1 && ! firstDelim )
			inserts++;
		else if ( h == dh && ! firstDelim )
			skips++;
		else if ( ! bro )
			inserts++;

		// sometimes we have a couple of back to back lines
		// that are like "M-F 8-5\n" and "Saturdays 8-6" and we do not
		// want them to make implied sections because it would
		// split them up wierd like for unm.edu.
		// unm.edu had 3 sentences:
		// "9 am. - 6 pm. Mon. - Sat.\n"
		// "Thur. 9 am. - 7 pm. Sun. 10 am - 4 pm.\n"
		// "Books, Furniture, Toys, TV's, Jewelry, Household Items\n"
		// and we were making an implied section around the last
		// two sentences, which messed everything up.
		// so let's add this code here to fix that.
		if ( h == dh && 
		     // this means basically back-to-back delimeters
		     brosWithWords <= 1 && 
		     // if we got a timeofday that is indicative of a schedule
		     (bro->m_flags & SEC_HAS_TOD) &&
		     ! firstDelim )
			return -2;

		// reset some stuff
		if ( h == dh ) {
			firstDelim = false;
			brosWithWords = 0;
			prevPrevStart = prevStart;
			prevStart = bro;
		}

		// . count sections.
		// . now we only count if they have text to avoid pointless
		//   implied sections for folkmads.org
		// . do not count delimeter sections towards this since
		//   delimeter sections START a partition cell
		if ( bro && bro->m_firstWordPos >= 0 )
			brosWithWords++;

		// keep a max on # of brothers with words in a given
		// partition cell. if all have just one such section
		// then no need to paritition at all!
		if ( brosWithWords > maxBrosWithWords )
			maxBrosWithWords = brosWithWords;

		// scan all sentences in this section and use them to
		// make a vector which we store in the hashtable "vht"
		for ( Section *sx=bro; sx &&sx->m_a<bro->m_b;sx = sx->m_next) {
			// mdwmdw break;
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not sentence
			if ( ! (sx->m_flags & SEC_SENTENCE) ) continue;
			// . store its vector components into hashtable
			// . store tagids preceeding sentence
			// . store tagid pairs preceeding sentence
			// . store SENT_* flags
			// . store SENT_* flags with prev sent SENT_* flags
			// . store hash of first word? last word?
			// . store SEC_HAS_MONTH/TOD/DOM/...
			// . should particular singles or pairs be weighted
			//   more than normal when doing the final compare
			//   between two adjacent implied sections in
			//   the partition?
			if (!hashSentBits(sx  ,&vht,container,0,dbt,NULL))
				return -3;
			if (!hashSentPairs(prevSent,sx,&vht,container,dbt))
				return -3;
			// set this
			prevSent = sx;
		}
		// stop when brother list exhausted
		if ( ! bro ) break;
	}

	// cache it
	if ( ! m_ct.addKey ( &dh ) ) return -3;

	// if no winners mdwmdw
	if ( minSim > 100.0 ) return -2;

	if ( maxBrosWithWords <= 1 ) return -2;

	if ( inserts <= 1 ) return -2;

	// at least one brother must NOT be a delimeter section and have text
	if ( nonDelims <= 0 ) return -2;

	// . METHOD_TAGID has to be really strong to work, since tags are
	//   so flaky in general
	// . dailylobo.com depends on this but shold probably use a
	//   METHOD_ABOVE_TOD_PURE or something or do hr splits before
	//   adding implied sections?
	// . this should also fix santafeplayhouse.org from adding tagid
	//   based implied sections that are screwing it up by grouping its
	//   tods up with the address below rather than the address above,
	//   since the address below is already claimed by the 6pm parking time
	if ( minSim < 80.00 && method == METHOD_TAGID ) return -2;

	if ( minSim < 80.00 && method == METHOD_INNER_TAGID ) return -2;

	// super bonus if all 100% (exactly the same)
	float avgSim = simTotal / simCount;

	// use this now
	// . very interesting: it only slighly insignificantly changes
	//   one url, graypanthers, if we use the avgSim vs. the minSim.
	// . i prefer minSim because it does not give an advantage to 
	//   many smaller sections vs. fewer larger sections in partition.
	// . later we should probably consider doing a larger partition first
	//   then paritioning those larger sections further. like looking 
	//   ahead a move in a chess game. should better partition 
	//   santafeplayhouse.org methinks this way
	//bonus1 = (int32_t)(avgSim * 100);
	bonus1 = (int32_t)(minSim * 1000);

	// i added the "* 2" to fix unm.edu because it was starting each
	// section the the <table> tag section but had a high badCount,
	// so this is kind of a hack...
	//int32_t total = goodCount * 10000 - badCount * 2 * 9000;
	int32_t total = 0;
	total += bonus1;

	// debug output
	char *ms = "";
	if ( method == METHOD_TAGID      ) ms = "tagid";
	if ( method == METHOD_DOM        ) ms = "dom";
	if ( method == METHOD_DOM_PURE   ) ms = "dompure";
	if ( method == METHOD_ABOVE_DOM  ) ms = "abovedom";
	if ( method == METHOD_DOW        ) ms = "dow";
	if ( method == METHOD_DOW_PURE   ) ms = "dowpure";
	if ( method == METHOD_MONTH_PURE ) ms = "monthpure";
	if ( method == METHOD_ABOVE_DOW  ) ms = "abovedow";
	if ( method == METHOD_INNER_TAGID ) ms = "innertagid";
	if ( method == METHOD_ABOVE_ADDR ) ms = "aboveaddr";
	//if ( method == METHOD_ABOVE_TOD  ) ms = "abovetod";
	//if ( method == METHOD_EMPTY_TAG  ) ms = "emptytag";
	//if ( method == METHOD_ATTRIBUTE ) ms = "attribute";

	// skip this for now
	//return total;

	logf(LOG_DEBUG,"sec: 1stbro=%"UINT32" "
	     "nondelims=%"INT32" "
	     "total=%"INT32" "
	     "bonus1=%"INT32" "
	     "bonus2=%"INT32" "
	     "bonus3=%"INT32" "
	     "avgSim=%.02f "
	     "minSim=%.02f "
	     "totalcomps=%"INT32" "
	     "inserts=%"INT32" "
	     "skips=%"INT32" "
	     "containera=%"INT32" "
	     //"goodcount=%"INT32" badcount=%"INT32" "
	     "dhA=%"INT32" method=%s",
	     start->m_a,
	     nonDelims,
	     total,
	     bonus1,
	     bonus2,
	     bonus3,
	     avgSim,
	     minSim,
	     minTotalComponents,
	     inserts,
	     skips,
	     (int32_t)container->m_a,
	     //goodCount,badCount,
	     delim->m_a,ms);

	// show the difference in the two adjacent brother sections that
	// resulted in the min similarity
	// NOTE: using the log() it was truncating us!! so use stderr
	fprintf(stderr,"sec: mina1=%"INT32" mina2=%"INT32" "
	     "missingbits=%s\n", mina1,mina2, minBuf.getBufStart());

	// return score
	return total;
}


char *getSentBitLabel ( sentflags_t sf ) {
	if ( sf == SENT_HAS_COLON  ) return "hascolon";
	if ( sf == SENT_AFTER_COLON  ) return "aftercolon";
	if ( sf == SENT_BAD_FIRST_WORD ) return "badfirstword";
	if ( sf == SENT_MIXED_CASE ) return "mixedcase";
	if ( sf == SENT_MIXED_CASE_STRICT ) return "mixedcasestrict";
	if ( sf == SENT_POWERED_BY ) return "poweredby";
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
	if ( sf == SENT_PRICEY ) return "pricey";
	if ( sf == (sentflags_t)SENT_HAS_PRICE ) return "hasprice";
	if ( sf == SENT_PERIOD_ENDS ) return "periodends";
	if ( sf == SENT_HAS_PHONE ) return "hasphone";
	if ( sf == SENT_IN_MENU ) return "inmenu";
	if ( sf == SENT_MIXED_TEXT ) return "mixedtext";
	if ( sf == SENT_TAGS ) return "senttags";
	if ( sf == SENT_INTITLEFIELD ) return "intitlefield";
	if ( sf == SENT_INPLACEFIELD ) return "inplacefield";
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
	if ( sf == SENT_FORMTABLE_FIELD ) return "formtablefield";
	if ( sf == SENT_FORMTABLE_VALUE ) return "formtablevalue";
	if ( sf == SENT_IN_TAG ) return "intag";
	if ( sf == SENT_HASSOMEEVENTSDATE ) return "hassomeeventsdate";
	if ( sf == SENT_HASTITLEWORDS ) return "hastitlewords";
	if ( sf == SENT_BADEVENTSTART ) return "badeventstart";
	if ( sf == SENT_MENU_SENTENCE ) return "menusentence";

	char *xx=NULL;*xx=0;
	return NULL;
}

static sentflags_t s_sentFlags[] = {
	SENT_AFTER_SPACER,
	SENT_BEFORE_SPACER,
	SENT_IN_TAG,
	SENT_IN_HEADER,
	SENT_NUMBERS_ONLY,
	SENT_IS_DATE,
	SENT_NUMBER_START,
	SENT_IN_TITLEY_TAG,
	SENT_PRICEY,
	SENT_HAS_PHONE,
	SENT_IN_MENU,
	SENT_INTITLEFIELD,
	SENT_STRANGE_PUNCT,
	SENT_INNONTITLEFIELD,
	SENT_NON_TITLE_FIELD,
	SENT_TITLE_FIELD,
	SENT_FIELD_NAME,
	SENT_PERIOD_ENDS_HARD,
	SENT_MIXED_CASE };

char *getSecBitLabel ( sec_t sf ) {
	if ( sf == SEC_HEADING_CONTAINER ) return "headingcontainer";
	if ( sf == SEC_PLAIN_TEXT ) return "plaintext";
	if ( sf == SEC_HAS_TOD   ) return "hastod";
	if ( sf == SEC_HAS_DOW   ) return "hasdow";
	if ( sf == SEC_HAS_MONTH ) return "hasmonth";
	if ( sf == SEC_HAS_DOM   ) return "hasdom";
	char *xx=NULL;*xx=0;
	return NULL;
}

static sec_t s_secFlags[] = {
	SEC_HEADING_CONTAINER,
	SEC_PLAIN_TEXT,
	SEC_HAS_TOD,
	SEC_HAS_DOW,
	SEC_HAS_MONTH,
	SEC_HAS_DOM };

#define MAXLABELSIZE 64

bool addLabel ( HashTableX *labelTable ,
		int32_t        key ,
		char       *label ) {
	// if in there, make sure agrees
	char *ptr = (char *)labelTable->getValue (&key);
	if ( ptr ) {
		// compare sanity check
		if ( strcmp(ptr,label) ) { char *xx=NULL;*xx=0; }
		return true;
	}

	// see if label already exists under different key
	for ( int32_t i = 0 ; i < labelTable->m_numSlots ; i++ ) {
		if ( ! labelTable->m_flags[i] ) continue;
		char *v = (char *)labelTable->getValueFromSlot(i);
		if ( strcmp(v,label) ) continue;
		int32_t k1 = *(int32_t *)labelTable->getKeyFromSlot(i);
		log("sec: key=%"INT32" oldk=%"INT32"",key,k1);
		char *xx=NULL;*xx=0;
	}

	// add it otherwise
	return labelTable->addKey ( &key, label );
}

// . if sx is NULL then prevSent will be the last sentence in partition cell
// . use the hashtable
bool Sections::hashSentBits (Section    *sx         ,
			     HashTableX *vht        ,
			     Section    *container  ,
			     uint32_t    mod        ,
			     HashTableX *labelTable ,
			     char       *modLabel   ) {

	int32_t n;
	int32_t count = 0;
	char sbuf [ MAXLABELSIZE ];
	
	if ( ! sx ) {
		// if no mod, we do not hash sent bits for NULL sentences
		if ( ! mod ) return true;
		// mix up
		uint32_t key = (mod << 8) ^ 72263;
		// use that
		if ( ! vht->addTerm32 ( &key) )  return false;
		// if no debug, done
		if ( ! labelTable ) return true;
		// for debug
		sprintf(sbuf,"%sXXXX",modLabel);
		addLabel(labelTable,key,sbuf);
		return true;
	}

	n = sizeof(s_sentFlags)/sizeof(sentflags_t);
	// handle SENT_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if bit is off
		if ( ! (sx->m_sentFlags & s_sentFlags[i] ) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getSentBitLabel ( s_sentFlags[i] );
		// store in buffer
		if ( modLabel ) 
			sprintf ( sbuf,"%s%s", modLabel,str );
		else    
			sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}

	n = sizeof(s_secFlags)/sizeof(sec_t);
	// and for SEC_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && ! (sx->m_flags & s_secFlags[i]) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getSecBitLabel ( s_secFlags[i] );
		// store in buffer
		if ( modLabel ) 
			sprintf ( sbuf,"%s%s", modLabel,str );
		else    
			sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}

	// and tag sections we are in
	for ( Section *sp = sx->m_parent ; sp ; sp = sp->m_parent ) {
		// stop when not in container anymore
		if ( container && ! container->contains ( sp ) ) break;
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no tag id
		if ( ! sp->m_tagId ) continue;
		// otherwise, use it
		uint32_t key = sp->m_tagId;
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getTagName(sp->m_tagId);
		if ( modLabel ) sprintf ( sbuf,"%s%s", modLabel,str);
		else            sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}		

	return true;
}

// . if sx is NULL then prevSent will be the last sentence in partition cell
// . use the hashtable
bool Sections::hashSentPairs (Section    *sx ,
			      Section    *sb ,
			      HashTableX *vht        ,
			      Section    *container  ,
			      HashTableX *labelTable ) {

	// only one can be NULL
	if ( ! sx && ! sb ) return true;

	int32_t n;
	int32_t count = 0;
	char *str = NULL;
	char sbuf [ MAXLABELSIZE ];

	if ( ! sx ) {
		// mix up
		uint32_t mod = 9944812;
		// for debug
		sprintf(sbuf,"XXXX*");
		// try to pair up with that
		return hashSentBits ( sb, vht,container,mod,labelTable,sbuf);
	}


	n = sizeof(s_sentFlags)/sizeof(sentflags_t);
	// handle SENT_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && ! ( sx->m_sentFlags & s_sentFlags[i]) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod that
		uint32_t mod = key << 8;
		// add to our label table too
		if ( labelTable ) {
			// convert sentence bit to text description
			char *str = getSentBitLabel ( s_sentFlags[i] );
			// store in buffer
			sprintf ( sbuf , "%s*", str );
			// make sure X chars or less
			if(strlen(sbuf)>=MAXLABELSIZE-1){char *xx=NULL;*xx=0;}
		}
		// hash sentenceb with that mod
		if ( ! hashSentBits ( sb, vht,container,mod,labelTable,sbuf)) 
			return false;
	}

	n = sizeof(s_secFlags)/sizeof(sec_t);
	// and for SEC_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && !(sx->m_flags & s_secFlags[i] ) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod that
		uint32_t mod = key << 8;
		// add to our label table too
		if ( labelTable ) {
			// convert sentence bit to text description
			char *str = getSecBitLabel ( s_secFlags[i] );
			// store in buffer
			sprintf ( sbuf , "%s*", str );
			// make sure X chars or less
			if(strlen(sbuf)>=MAXLABELSIZE-1){char *xx=NULL;*xx=0;}
		}
		// hash sentenceb with that mod
		if ( ! hashSentBits ( sb, vht, container,mod,labelTable,sbuf)) 
			return false;
	}

	// and tag sections we are in
	for ( Section *sp = sx->m_parent ; sp ; sp = sp->m_parent ) {
		// stop when not in container anymore
		if ( container && ! container->contains ( sp ) ) break;
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no tag id
		if ( ! sp->m_tagId ) continue;
		// otherwise, use it
		uint32_t key = sp->m_tagId;
		// mod that
		uint32_t mod = (key<<8) ^ 45644;
		// fake it
		if ( labelTable ) {
			// convert sentence bit to text description
			sprintf(sbuf,"%s*", getTagName(sp->m_tagId) );
			str = sbuf;
		}
		// if no mod, then do the pairs now
		if ( ! hashSentBits ( sb,vht,container,mod,labelTable,str)) 
			return false;
	}

	return true;
}


// . don't return 0 because we make a vector of these hashes
//   and computeSimilarity() assumes vectors are NULL term'd. return -1 instead
int32_t Sections::getDelimHash ( char method , Section *bro ) {

	// now all must have text!
	//if ( bro->m_firstWordPos < 0 ) return -1;

	// if has no text give it a slightly different hash because these
	// sections are often seen as delimeters
	int32_t mod = 0;
	if ( bro->m_firstWordPos < 0 ) mod = 3405873;

	// . single br tags not allowed to be implied section delimeters any
	//   more for no specific reason but seems to be the right way to go
	// . this hurts the guysndollsllc.com url, which has single br lines
	//   each with its own DOM, so let's allow br tags in that case
	if ( m_tids[bro->m_a] == TAG_BR && bro->m_a + 1 == bro->m_b  ) 
		return -1;

	if ( method == METHOD_MONTH_PURE ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_MONTH) ) 
			return -1;
		// skip if also have daynum, we just want the month and
		// maybe the year...
		if ( (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// now it must be all date words
		int32_t a = bro->m_firstWordPos;
		int32_t b = bro->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
		}
		// do not collide with tagids
		return 999999;
	}
	// are we partitioning by tagid?
	if ( method == METHOD_TAGID ) {
		int32_t tid = bro->m_tagId;
		// . map a strong tag to h4 since they look the same!
		// . should fix metropolis url
		if ( tid == TAG_STRONG ) tid = TAG_H4;
		if ( tid == TAG_B      ) tid = TAG_H4;
		if ( tid ) return tid;
		// . use -1 to indicate can't be a delimeter
		// . this should fix switchboard.com which lost
		//   "Wedding cakes..." as part of description because it
		//   called it an SEC_MENU_HEADER because an implied section
		//   was inserted and placed it at the top so it had nothing
		//   above it so-to-speak and was able to be a menu header.
		if ( bro->m_baseHash == BH_SENTENCE ) return -1;
		// if 0 use base hash
		return bro->m_baseHash ^ mod;
	}

	if ( method == METHOD_INNER_TAGID ) {
		Section *last = bro;
		// scan to right to find first kid
		for ( Section *nn = bro->m_next ; nn ; nn = nn->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if not contained
			if ( ! last->contains ( nn ) ) break;
			// ignore sentences because they alternate with
			// the strong tags used in abqfolkfest.org and
			// mess things up. some are <x><strong><sent> and
			// some are <x><sent>blah blah<strong>.
			// THIS IS NOT THE BEST WAY to fix this issue. i think
			// we need to put the sentence sections outside the
			// strong sections when possible? not sure.
			if ( nn->m_baseHash == BH_SENTENCE ) continue;
			// stop if it directly contains text DIRECTLY 
			// to fix <p> blah <a href> blah</a> </p> for
			// abqtango.com, so we stop at the <p> tag and do
			// not use the <a> tag as the inner tag id.
			// CRAP, we do not have this set yet...
			//if ( ! ( nn->m_flags & SEC_NOTEXT ) ) break;
			// just do it this way then
			if ( nn->m_a +1 < m_nw && 
			     m_wids[nn->m_a + 1] ) 
				break;
			if ( nn->m_a +2 < m_nw && 
			     ! m_tids[nn->m_a+1] &&
			     m_wids[nn->m_a + 2] ) 
				break;
			// update
			last = nn;
		}
		// do not allow sentences (see comment above)
		if ( last->m_baseHash == BH_SENTENCE ) return -1;
		// make it a little different from regular tagid
		return last->m_tagId ^ 34908573 ^ mod;
	}
	if ( method == METHOD_DOM ) {
		// need a dom of course (day of month)
		if ( ! (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// we require it also have another type because DOM is
		// quite fuzzy and matches all these little numbers in
		// xml docs like stubhub.com's xml feed
		if ( ! (bro->m_flags & 
			(SEC_HAS_MONTH|SEC_HAS_DOW|SEC_HAS_TOD)) )
			return -1;
		// if you aren't pure you should at least be in a tag
		// or something. don't allow regular sentences for now...
		// otherwise we get a few sections that aren't good for
		// sunsetpromotions.com because it has a few sentences 
		// mentioning the day of month (april 16 2011). but those
		// sections should be split by the double br tags.
		if ( bro->m_sentFlags & SENT_MIXED_CASE )
			return -1;
		// . must be a sort of heading like "Jul 24"
		// . without this we were getting bad implied sections
		//   for tennisoncampus because the section had a sentence
		//   with a bunch of sentences... BUT what does this hurt?? 
		// . it hurts anja when we require this stuff here on
		//   texasdrums.drums.org/new_orleansdrums.htm beause it is
		//   unclear that her event should be boxed in...
		if ( !(bro->m_flags & SEC_SENTENCE) &&
		     !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		// do not collide with tagids
		return 11111;
	}
	if ( method == METHOD_DOM_PURE ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// now it must be all date words
		int32_t a = bro->m_firstWordPos;
		int32_t b = bro->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
		}
		// do not collide with tagids
		return 55555;
	}
	if ( method == METHOD_ABOVE_DOM ) {
		// we cannot have a dom ourselves to reduce the problem
		// with cabq.gov/museums/events.html of repeating the dom
		// in the body and getting a false header.
		if ( bro->m_flags & SEC_HAS_DOM )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		// skip empties like image tags
		if ( nb->m_firstWordPos < 0 ) 
			nb = nb->m_nextBrother;
		if ( ! nb ) 
			return -1;
		if ( ! ( nb->m_flags & SEC_HAS_DOM ) ) 
			return -1;
		// require we be in a tag
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		// do not collide with tagids
		return 22222;
	}
	if ( method == METHOD_DOW ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_DOW) ) 
			return -1;
		// do not collide with tagids
		return 33333;
	}
	if ( method == METHOD_DOW_PURE ) {
		// santafeplayhouse.org had 
		// "Thursdays Pay What You Wish Performances" and so it
		// was not getting an implied section set, so let's do away
		// with the pure dow algo and see what happens.
		return -1;
	}
	if ( method == METHOD_ABOVE_DOW ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		if ( ! ( nb->m_flags & SEC_HAS_DOW ) ) 
			return -1;
		// next sentence not set yet, so figure it out
		Section *sent = nb;
		// scan for it
		for ( ; sent ; sent = sent->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop we got a sentence section now
			if ( sent->m_flags & SEC_SENTENCE ) break;
		}
		// might have been last sentence already
		if ( ! sent )
			return -1;
		// . next SENTENCE must have the dow
		// . should fix santafeplayhouse from making crazy
		//   implied sections
		if ( ! (sent->m_flags & SEC_HAS_DOW) )
			return -1;
		// do not collide with tagids
		return 44444;
	}

	char *xx=NULL;*xx=0;
	return 0;
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

// identify crap like "Good For Kids: Yes" for yelp.com etc. so we don't
// use that crap as titles.
bool Sections::setFormTableBits ( ) {

	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	sec_t sdf = SEC_HAS_DOM|SEC_HAS_MONTH|SEC_HAS_DOW|SEC_HAS_TOD;
	// scan all sentences
	for ( Section *si = m_firstSent ; si ; si = si->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be sentence
		//if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// shortcut
		sentflags_t sf = si->m_sentFlags;
		// . fortable field must not end in period
		// . i.e. "Good For Kids:"
		if ( sf & SENT_PERIOD_ENDS ) continue;
		// get next sent
		Section *next = si->m_nextSent;
		// this stops things..
		if ( ! next ) continue;
		// must not end in period either
		if ( next->m_sentFlags & SENT_PERIOD_ENDS ) continue;
		Section *ps;
		// must be the only sentences in a section
		ps = si->m_parent;
		for ( ; ps ; ps = ps->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ps->contains ( next ) ) break;
		}
		// how does this happen?
		if ( ! ps ) continue;
		// must not contain any other sentences, just these two
		if ( ps->m_alnumPosA != si  ->m_alnumPosA )
			continue;
		if ( ps->m_alnumPosB != next->m_alnumPosB )
			continue;
		// get first solid parent tag for "si"
		Section *bs = si->m_parent;
		for ( ; bs ; bs = bs->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( bs->m_tagId == TAG_B ) continue;
			if ( bs->m_tagId == TAG_STRONG ) continue;
			if ( bs->m_tagId == TAG_FONT ) continue;
			if ( bs->m_tagId == TAG_I ) continue;
			break;
		}
		// if none, bail
		if ( ! bs ) continue;
		// get the containing tag then
		nodeid_t tid = bs->m_tagId;
		// must be some kind of field/value indicator
		bool separated = false;
		if ( tid == TAG_DT ) separated = true;
		if ( tid == TAG_TD ) separated = true;
		// if tr or dt tag or whatever contains "next" that is not
		// good, we need next and si to be in their own dt or td
		// section.
		if ( bs->contains ( next ) ) separated = false;
		// fix "Venue Type: Auditorium" for zvents.com kimo theater
		if ( sf & SENT_COLON_ENDS ) separated = true;
		// must be separated by a tag or colon to be field/value pair
		if ( ! separated ) continue;
		// if either one has dates, let is slide.
		// fix "zumba</td><td>10/26/2011" for www.ci.tualatin.or.us
		if ( si  ->m_flags & sdf ) continue;
		if ( next->m_flags & sdf ) continue;
		// label them
		si  ->m_sentFlags |= SENT_FORMTABLE_FIELD;
		next->m_sentFlags |= SENT_FORMTABLE_VALUE;
	}
	return true;
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


#include "gb-include.h"
#include "Threads.h"

Sectiondb g_sectiondb;
Sectiondb g_sectiondb2;

// reset rdb
void Sectiondb::reset() { m_rdb.reset(); }

// init our rdb
bool Sectiondb::init ( ) {
	// . what's max # of tree nodes?
        // . key+4+left+right+parents+dataPtr = sizeof(key128_t)+4 +4+4+4+4
        // . 32 bytes per record when in the tree
	int32_t node = 16+4+4+4+4+4 + sizeof(SectionVote);
	// . assume avg TitleRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	//int32_t maxTreeNodes  = g_conf.m_sectiondbMaxTreeMem  / node;
	int32_t maxTreeMem    = 200000000;
	int32_t maxTreeNodes  = maxTreeMem / node;

	// do not use for now i think we use posdb and store the 32bit
	// val in the key for facet type stuff
	//pcmem = 0;
	maxTreeMem = 100000;
	maxTreeNodes = 1000;

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir               ,
			    "sectiondb"                  ,
			    true                         , // dedup same keys?
			    sizeof(SectionVote)          , // fixed record size
			    // avoid excessive merging!
			    -1                           , // min files 2 merge
			    maxTreeMem                   ,
			    maxTreeNodes                 ,
			    true                         , // balance tree?
			    // turn off cache for now because the page cache
			    // is just as fast and does not get out of date
			    // so bad??
			    0                            ,
			    0                            , // maxCacheNodes
			    false                        , // half keys?
			    false                        , // saveCache?
			    NULL,//&m_pc                , // page cache ptr
			    false                        , // is titledb?
			    false                        , // preloadcache?
			    16                           ))// keySize
		return false;
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Sectiondb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
        // . key+4+left+right+parents+dataPtr = sizeof(key128_t)+4 +4+4+4+4
        // . 32 bytes per record when in the tree
	int32_t node = 16+4+4+4+4+4 + sizeof(SectionVote);
	// . NOTE: overhead is about 32 bytes per node
	int32_t maxTreeNodes  = treeMem / node;
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "sectiondbRebuild"          ,
			    true                        , // dedup same keys?
			    sizeof(SectionVote)         , // fixed record size
			    50                          , // MinFilesToMerge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    false                       , // half keys?
			    false                       , // sectiondbSaveCache
			    NULL                        , // page cache ptr
			    false                       , // is titledb?
			    false                       , // prelaod cache?
			    16                          ))// keySize
		return false;
	return true;
}

bool Sectiondb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Sectiondb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);

	if ( ! msg5.getList ( RDB_SECTIONDB   ,
			      cr->m_collnum          ,
			      &list         ,
			      &startKey      ,
			      &endKey        ,
			      1024*1024     , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        ,
			      false         )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key128_t k;
		list.getCurrentKey(&k);
		count++;
		uint32_t shardNum = getShardNum ( RDB_SECTIONDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in sectiondb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( count > 10 && got == 0 ) 
			log("db: Are you sure you have the right "
				   "data in the right directory? "
				   "Exiting.");
		log ( "db: Exiting due to Sectiondb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Sectiondb passed verification successfully for "
	      "%"INT32" recs.", count );
	// DONE
	g_threads.enableThreads();
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

