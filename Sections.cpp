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
#include "Conf.h"
#include "XmlDoc.h"
#include "Bits.h"
#include "sort.h"
#include "Abbreviations.h"
#include "Process.h"
#include "Posdb.h"

Sections::Sections ( ) {
	m_sections = NULL;
	reset();
}

void Sections::reset() {
	m_sectionBuf.purge();
	m_sectionPtrBuf.purge();

	m_sections         = NULL;
	m_bits             = NULL;
	m_numSections      = 0;
	m_rootSection      = NULL;
	m_lastSection      = NULL;
	m_lastAdded        = NULL;
	m_nw = 0;
	m_firstSent = NULL;
	m_sectionPtrs = NULL;
	
	// Coverity
	m_sbuf = NULL;
	m_words = NULL;
	m_url = NULL;
	m_coll = NULL;
	m_contentType = 0;
	m_wposVec = NULL;
	m_densityVec = NULL;
	m_wordSpamVec = NULL;
	m_fragVec = NULL;
	m_isRSSExt = false;
	m_titleStart = 0;
	m_maxNumSections = 0;
	memset(m_localBuf, 0, sizeof(m_localBuf));
	m_wids = NULL;
	m_wlens = NULL;
	m_wptrs = NULL;
	m_tids = NULL;
	m_hiPos = 0;
}

Sections::~Sections ( ) {
	reset();
}

#define TXF_MATCHED 1

// an element on the stack is a Tag
class Tagx {
public:
	// id of the fron tag we pushed
	nodeid_t m_tid;
	// section number we represent
	int32_t     m_secNum;
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
bool Sections::set( Words *w, Bits *bits, Url *url, char *coll, uint8_t contentType ) {
	reset();

	if ( ! w ) return true;

	if ( w->getNumWords() > 1000000 ) {
		log("sections: over 1M words. skipping sections set for "
		    "performance.");
		return true;
	}

	// save it
	m_words           = w;
	m_bits            = bits;
	m_url             = url;
	m_coll            = coll;
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

	// set these up
	m_wids  = wids;
	m_wlens = wlens;
	m_wptrs = wptrs;
	m_tids  = tids;

	m_isRSSExt = false;
	const char *ext = m_url->getExtension();
	if ( ext && strcasecmp(ext,"rss") == 0 ) m_isRSSExt = true;
	if ( m_contentType == CT_XML ) m_isRSSExt = true;

	// . how many sections do we have max?
	// . init at one to count the root section
	int32_t max = 1;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// . count all front tags

		// count back tags too since some url 
		// http://www.tedxhz.com/tags.asp?id=3919&id2=494 had a bunch
		// of </p> tags with no front tags and it cored us because
		// m_numSections > m_maxNumSections!
		if ( tids[i] ) {
			max += 2;
		// . any punct tag could have a bullet in it...
		// . or if its a period could make a sentence section
		} else if ( ! wids[i] ) {
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

	// truncate if excessive.
	if ( max > 1000000 ) {
		log("sections: truncating max sections to 1000000");
		max = 1000000;
	}

	int32_t need = max * sizeof(Section);

	// set this
	m_maxNumSections = max;

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

	m_titleStart = -1;

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

	// Sections are no longer 1-1 with words, just with front tags
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		nodeid_t fullTid = tids[i];

		// are we a non-tag?
		if ( ! fullTid ) { 
			continue;
		}

		// make a single section for input tags
		if ( fullTid == TAG_INPUT ||
		     fullTid == TAG_HR    ||
		     fullTid == TAG_COMMENT ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections) {
				g_errno = EDOCBADSECTIONS;
				return true;
			}
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { g_process.shutdownAbort(true); }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// section consists of just this tag
			sn->m_b = i + 1;
			// go on to next
			continue;
		}

		// a section of multiple br tags in a sequence
		if ( fullTid == TAG_BR ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections) {
				g_errno = EDOCBADSECTIONS;
				return true;
			}
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { g_process.shutdownAbort(true); }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// count em up
			int32_t brcnt = 1;
			// scan for whole sequence
			int32_t lastBrPos = i;
			for ( int32_t j = i + 1 ; j < nw ; j++ ) {
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

		// do not breach the stack
		if ( stackPtr - stack >= MAXTAGSTACK ) {
			log( LOG_WARN, "html: stack breach for %s",url->getUrl());
			// if we set g_errno and return then the url just
			// ends up getting retried once the spider lock
			// in Spider.cpp expires in MAX_LOCK_AGE seconds.
			// about an hour. but really we should completely
			// give up on this. whereas we should retry OOM errors
			// etc. but this just means bad html really.

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
			if ( xn<0 || xn>=m_numSections ) {g_process.shutdownAbort(true);}
			// get it
			Section *sn = &m_sections[xn];

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
				if ( ps->m_b > sn->m_b ) {g_process.shutdownAbort(true);}

				// cut our end shorter
				sn->m_b = ps->m_b;
				// our TXF_MATCHED bit should still be set
				// for spp->m_flags, so try to match ANOTHER
				// front tag with this back tag now
				if ( ! ( spp->m_flags & TXF_MATCHED ) ) {
					g_process.shutdownAbort(true); }
				// ok, try to match this back tag with another
				// front tag on the stack, because the front
				// tag we had selected got cut short because
				// its parent forced it to cut short.
				goto subloop;
			}
   
			// sanity check
			if ( sn->m_b <= sn->m_a ) { g_process.shutdownAbort(true);}

			// revert it to this guy, may not equal stackPtr-1 !!
			stackPtr = spp;
			
			// get parent section
			if ( stackPtr > stack ) {
				// get parent section now
				xn = (stackPtr-1)->m_secNum;
				// set current to that
				current = &m_sections[xn];
			}
			else {
				// i guess this is bad html!
				current = rootSection;
			}
			
			// debug log
			if ( g_conf.m_logDebugSections ) {
				const char *ms = "";
				if ( stackPtr->m_tid != ptid) ms =" UNMATCHED";
				const char *back ="";
				if ( fullPopTid & BACKBIT ) back = "/";
				logf(LOG_DEBUG,"section: pop tid=%" PRId32" "
				     "i=%" PRId32" "
				     "level=%" PRId32" "
				     "%s%s "
				     //"h=0x%" PRIx32
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

		if ( tid == TAG_CENTER ) continue;

		if ( tid == TAG_SPAN ) continue;
		// gwair.org has font tags the pair up a date "1st Sundays"
		// with the address above it, and it shouldn't do that!
		if ( tid == TAG_FONT ) continue;

		// try to realloc i guess. should keep ptrs in tact.
		if ( m_numSections >= m_maxNumSections) {
			g_errno = EDOCBADSECTIONS;
			return true;
		}

		// get the section
		Section *sn = &m_sections[m_numSections];

		// clear
		memset ( sn , 0 , sizeof(Section) );

		// inc it
		m_numSections++;

		// sanity check - breach check
		if ( m_numSections > max ) { g_process.shutdownAbort(true); }
		
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
		stackPtr->m_secNum      = m_numSections - 1;
		stackPtr->m_flags       = 0;
		stackPtr++;

		// debug log
		if ( ! g_conf.m_logDebugSections ) continue;

		logf(LOG_DEBUG,"section: push tid=%" PRId32" "
		     "i=%" PRId32" "
		     "level=%" PRId32" "
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
		if ( sp->m_a > si->m_a ) { g_process.shutdownAbort(true); }
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
		// get it
		Section *si = &m_sections[i];
		// get its parent
		Section *ps = si->m_parent;
		// if parent is open-ended panic!
		if ( ps && ps->m_b < 0 ) { g_process.shutdownAbort(true); }

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
		if ( ! tid1 ) { g_process.shutdownAbort(true); }
		// NOW, see if within that parent there is actually another
		// tag after us of our same tag type, then use that to
		// constrain us instead!!
		// this hurts <p><table><tr><td><p>.... because it
		// uses that 2nd <p> tag to constrain si->m_b of the first
		// <p> tag which is not right! sunsetpromotions.com has that.
		for ( int32_t j = i + 1 ; j < m_numSections ; j++ ) {
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
		if ( si->m_b < 0 ) { g_process.shutdownAbort(true); }
		// get parent
		Section *sp = si->m_parent;
		// skip if null
		if ( ! sp ) continue;
		// skip if parent still open ended
		if ( sp->m_b < 0 ) { g_process.shutdownAbort(true); }
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
		if ( sp->m_b == -1 ) { g_process.shutdownAbort(true); }
		// get grandparent
		sp = sp->m_parent;
		// set
		si->m_parent = sp;
		// try again
		goto doagain2;
	}

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

	// . addImpliedSections() requires Section::m_baseHash
	// . set Section::m_baseHash
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
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
		//     !(sn->m_flags & SEC_SENTENCE) ) { g_process.shutdownAbort(true); }
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
		if ( sn->m_flags & SEC_FAKE     ) { g_process.shutdownAbort(true); }
		if ( sn->m_flags & SEC_SENTENCE ) { g_process.shutdownAbort(true); }
		// sanity check
		if ( mtid == 0 ) { g_process.shutdownAbort(true); }
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
		}
		// set this now too WHY? should already be set!!! was
		// causing the root section to become a title section
		// because first word was "<title>". then every word in
		// the doc got SEC_IN_TITLE set and did not get hashed
		// in XmlDoc::hashBody()... NOR in XmlDoc::hashTitle()!!!
		if ( sn != rootSection )
			sn->m_tagId = tid;
	}

	// set up our linked list, the functions below will insert sections
	// and modify this linked list
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// set it
		if ( i + 1 < m_numSections )
			m_sections[i].m_next = &m_sections[i+1];
		if ( i - 1 >= 0 )
			m_sections[i].m_prev = &m_sections[i-1];
	}

	// init to -1 to indicate none
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// reset it
		si->m_firstWordPos = -1;
		si->m_lastWordPos  = -1;
		si->m_senta        = -1;
		si->m_sentb        = -1;
	}
	// now set position of first word each section contains
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
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
		else if ( tid == TAG_NOSCRIPT) mf = SEC_NOSCRIPT;
		else if ( tid == TAG_STYLE   ) mf = SEC_STYLE;
		else if ( tid == TAG_SELECT  ) mf = SEC_SELECT;
		else if ( tid == TAG_H1      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H2      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H3      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H4      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_TITLE   ) mf = SEC_IN_TITLE;
		else if ( tid == TAG_HEAD    ) mf = SEC_IN_HEAD;

		// accumulate
		inFlag |= mf;

		// add in the flags
		si->m_flags |= inFlag;

		// skip if nothing special
		if ( ! mf ) continue;

		// sanity
		if ( ni >= 1000 ) { g_process.shutdownAbort(true); }

		// otherwise, store on stack
		istack[ni] = si->m_b;
		iflags[ni] = mf;
		ni++;

		// title is special
		if ( tid == TAG_TITLE && m_titleStart == -1 ) {
			m_titleStart = si->m_a; // i;
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
	}

	// . set m_nextBrother
	// . we call this now to aid in setHeadingBit() and for adding the
	//   implied sections, but it is ultimately
	//   called a second time once all the new sections are inserted
	setNextBrotherPtrs ( false );

	// . set SEC_HEADING bit
	// . need this before implied sections
	setHeadingBit ();

	setTagHashes();

	//
	//
	// TODO TODO
	//
	// TAKE OUT THESE SANITY CHECKS TO SPEED UP!!!!!!
	//
	//

	// clear this
	bool isHidden  = false;
	int32_t startHide = 0x7fffffff;
	int32_t endHide   = 0 ;
	// now that we have closed any open tag, set the SEC_HIDDEN bit
	// for all sections that are like <div style=display:none>
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// set m_lastSection so we can scan backwards
		m_lastSection = sn;

		// set this
		int32_t wn = sn->m_a;
		// stop hiding it?
		if ( isHidden ) {
			// turn it off if not contained
			if      ( wn >= endHide   ) isHidden = false;
			else    sn->m_flags |= SEC_HIDDEN;
		}
		// get tag id
		nodeid_t tid = sn->m_tagId;
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
		// must be an alnum word
		if ( ! m_wids[i] ) continue;
		// get its section
		m_sectionPtrs[i]->m_contentHash64 ^= m_wids[i];
		// fix "smooth smooth!"
		if ( m_sectionPtrs[i]->m_contentHash64 == 0 )
			m_sectionPtrs[i]->m_contentHash64 = 123456;

	}

	// now set SEC_NOTEXT flag if content hash is zero!
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( sn->m_contentHash64 ) continue;
		// no text!
		sn->m_flags |= SEC_NOTEXT;
	}

	//
	// set Section::m_alnumPosA/m_alnumPosB
	//
	int32_t alnumCount2 = 0;
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
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
			// must be an alnum word
			if ( ! m_wids[j] ) continue;
			// alnumcount
			alnumCount2++;
		}
		// so we contain the range [a,b), typical half-open interval
		sn->m_alnumPosB = alnumCount2;
		// sanity check
		if ( sn->m_alnumPosA == sn->m_alnumPosB ){g_process.shutdownAbort(true);}

		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one!
			if ( si->m_alnumPosA > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosA = sn->m_alnumPosA;
		}

	}
	// propagate up alnumPosB now
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one! no, because we need to
			// get the MAX of all of our kids!!
			//if ( si->m_alnumPosB > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosB = sn->m_alnumPosB;
		}
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
	setNextBrotherPtrs ( true );

	///////////////////////////////////////
	//
	// now set SEC_MENU and SEC_LINK_TEXT flags
	//
	///////////////////////////////////////
	setMenus();

	//verifySections();

	return true;
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
	sec_t badFlags = SEC_STYLE | SEC_SCRIPT | SEC_SELECT | SEC_HIDDEN | SEC_NOSCRIPT;

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
	m_bits->setInUrlBits ( );
	// shortcut
	wbit_t *bb = m_bits->m_bits;

	// is the abbr. a noun? like "appt."
	bool hasWordAfter = false;

	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
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
		int32_t     lastk = 0;
		Section *splitSection = NULL;
		Section *lastGuy = NULL;

		for ( int32_t k = senta ; k <= sentb ; k++ ) {
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
				if ( m_wids[adda] ) { g_process.shutdownAbort(true); }
			}
			// sanity
			if ( adda < 0 ) { g_process.shutdownAbort(true); }

			// same for right endpoint
			for ( ; addb < m_nw ; ) {
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
				if ( m_wids[addb] ) { g_process.shutdownAbort(true); }
			}
			// sanity
			if ( addb >= m_nw ) { g_process.shutdownAbort(true); }

			// ok, now add the split sentence
			Section *is =insertSubSection(adda,addb+1,bh);
			// panic?
			if ( ! is )
				break;
			// set sentence flag on it
			is->m_flags |= SEC_SENTENCE;
			// . set this
			// . sentence is from [senta,sentb)
			is->m_senta = senta;//start;
			is->m_sentb = sentb;//k;
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
	// try to realloc i guess. should keep ptrs in tact.
	if ( m_numSections >= m_maxNumSections ) {
		g_errno = EDOCBADSECTIONS;
		return NULL;
	}

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

	// get first section containing word #a
	Section *si = m_sectionPtrs[a];

	for ( ; si ; si = si->m_prev ) {
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
	if ( m_lastAdded && si && m_lastAdded->m_a > si->m_a && m_lastAdded->m_a < a ) {
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
		g_process.shutdownAbort(true);
		return NULL;
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
		if ( parent->m_a > a ) continue;
		if ( parent->m_b < b ) continue;
		break;
	}
	// now we assign the parent to you
	sk->m_parent    = parent;
	// sometimes an implied section is a subsection of a sentence!
	// like when there are a lot of brbr (double br) tags in it...
	sk->m_sentenceSection = parent->m_sentenceSection;
	// take out certain flags from parent
	sec_t flags = parent->m_flags;
	flags &= ~SEC_SENTENCE;

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
		// sanity check
		if ( wp->m_b > sk->m_b ) { g_process.shutdownAbort(true); }
		if ( wp->m_a < sk->m_a ) { g_process.shutdownAbort(true); }
	}

	return sk;
}

// this is a function because we also call it from addImpliedSections()!
void Sections::setNextBrotherPtrs ( bool setContainer ) {

	// clear out
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		si->m_nextBrother = NULL;
		si->m_prevBrother = NULL;
	}

	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
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
			g_process.shutdownAbort(true); }
		// set brother
		si->m_nextBrother = sj;
		// set his prev then
		sj->m_prevBrother = si;
		// sanity check
		if ( sj->m_parent != si->m_parent ) { g_process.shutdownAbort(true); }
		// sanity check
		if ( sj->m_a < si->m_b &&
		     sj->m_tagId != TAG_TC &&
		     si->m_tagId != TAG_TC ) { 
			g_process.shutdownAbort(true); }
		// do more?
		if ( ! setContainer ) continue;
		// telescope this
		Section *te = sj;
		// telescope up until it contains "si"
		for ( ; te && te->m_a > si->m_a ; te = te->m_parent );
		// only update list container if smaller than previous
		if ( ! si->m_listContainer )
			si->m_listContainer = te;
		else if ( te && te->m_a > si->m_listContainer->m_a )
			si->m_listContainer = te;
		if ( ! sj->m_listContainer )
			sj->m_listContainer = te;
		else if ( te && te->m_a > sj->m_listContainer->m_a )
			sj->m_listContainer = te;

		// now 
	}
}

void Sections::setNextSentPtrs ( ) {
	// kinda like m_rootSection
	m_firstSent = NULL;

	Section *finalSec = NULL;

	// scan the sentence sections and number them to set m_sentNum
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// record final section
		finalSec = sk;

		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) {
			continue;
		}

		// first one?
		if ( ! m_firstSent ) {
			m_firstSent = sk;
		}
	}

	Section *lastSent = NULL;

	// now set "m_nextSent" of each section
	for ( Section *sk = finalSec ; sk ; sk = sk->m_prev ) {
		// set this
		sk->m_nextSent = lastSent;

		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) {
			continue;
		}

		// we are the sentence now
		lastSent = sk;
	}
}

#define TABLE_ROWS 25

void Sections::printFlags (SafeBuf *sbuf , Section *sn ) {
	sec_t f = sn->m_flags;

	if ( f & SEC_HEADING )
		sbuf->safePrintf("heading ");

	if ( f & SEC_MENU_SENTENCE )
		sbuf->safePrintf("menusentence " );
	if ( f & SEC_MENU )
		sbuf->safePrintf("ismenu " );
	if ( f & SEC_MENU_HEADER )
		sbuf->safePrintf("menuheader " );

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
		else { g_process.shutdownAbort(true); }
	}

	if ( f & SEC_NOTEXT )
		sbuf->safePrintf("notext ");

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

	if ( f & SEC_IN_HEAD )
		sbuf->safePrintf("inhead ");

	if ( f & SEC_IN_TITLE )
		sbuf->safePrintf("intitle ");

	if ( f & SEC_IN_HEADER )
		sbuf->safePrintf("inheader ");
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
		// . if we hit plain text, we kill our last
		// . this was causing "geeks who drink" for blackbirdbuvette
		//   to get is SEC_MENU set because there was a link after it
		if ( si->m_flags & SEC_PLAIN_TEXT ) {
			last = NULL;
		}

		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) {
			continue;
		}

		// . if it is a mailto link forget it
		// . fixes abtango.com from detecting a bad menu
		char *ptr  = m_wptrs[si->m_a];
		int32_t  plen = m_wlens[si->m_a];

		char *mailto = strncasestr(ptr,plen,"mailto:");
		if ( mailto ) {
			last = NULL;
		}

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
		if ( prev && prev->m_flags & SEC_PLAIN_TEXT ) continue;
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
		if ( sk && sk->m_flags & SEC_PLAIN_TEXT ) continue;

		// . first hard sections encountered must match!
		// . otherwise for switchborad.com we lose "A B C ..." as
		//   title candidate because we think it is an SEC_MENU
		//   because the sections before it have links in them, but
		//   they have different hard sections
		if (   prevHard && ! skHard ) continue;
		if ( ! prevHard &&   skHard ) continue;
		if ( prevHard && prevHard->m_tagId != skHard->m_tagId ) continue;

		// ok, great that works!
		if( prev ) {
			prev->m_flags |= SEC_MENU;
		}
		if( sk ) {
			sk->m_flags |= SEC_MENU;
		}
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

	sec_t ff = SEC_MENU;

	// set SEC_MENU of child sections of SEC_MENU sections
	for ( Section *si = m_rootSection; si; si = si->m_next ) {
		// must be a link text only section
		if ( !( si->m_flags & ff ) )
			continue;

		// ignore if went down this path
		if ( si->m_used == 82 ) {
			continue;
		}

		// get first potential kid
		Section *sk = si->m_next;
		// scan child sections
		for ( ; sk; sk = sk->m_next ) {
			// stop if not contained
			if ( !si->contains( sk ) ) {
				break;
			}

			// mark it
			sk->m_flags |= ( si->m_flags & ff ); // SEC_MENU;

			// ignore in big loop
			sk->m_used = 82;
		}
	}

	//
	// set SEC_MENU_HEADER
	//
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// skip if not in a menu
		if ( ! ( sk->m_flags & SEC_MENU ) ) {
			continue;
		}

		// get his list container
		Section *c = sk->m_listContainer;

		// skip if none
		if ( !c ) {
			continue;
		}

		// already flagged?
		if ( c->m_used == 89 ) {
			continue;
		}

		// do not repeat on any item in this list
		c->m_used = 89;

		// flag all its brothers!
		Section *zz = sk;
		for ( ; zz; zz = zz->m_nextBrother ) {
			// bail if not in menu
			if ( !( zz->m_flags & SEC_MENU ) ) {
				break;
			}
		}

		// if broked it, stop
		if ( zz ) {
			continue;
		}

		//
		// ok, every item in list is a menu item, so try to set header
		//
		// get word before first item in list
		int32_t r = sk->m_a - 1;
		for ( ; r >= 0 && !m_wids[r]; r-- )
			;

		// if no header, skip
		if ( r < 0 ) {
			continue;
		}

		// set SEC_MENU_HEADER
		setHeader( r, sk, SEC_MENU_HEADER );
	}

	//
	// set SEC_MENU_SENTENCE flag
	//
	for ( Section *si = m_rootSection; si; si = si->m_next ) {
		// must be a link text only section
		if ( !( si->m_flags & SEC_MENU ) ) {
			continue;
		}

		// set this
		bool gotSentence = ( si->m_flags & SEC_SENTENCE );

		// set SEC_MENU of the sentence
		if ( gotSentence ) {
			continue;
		}

		// parent up otherwise
		for ( Section *sk = si->m_parent; sk; sk = sk->m_parent ) {
			// stop if sentence finally
			if ( !( sk->m_flags & SEC_SENTENCE ) ) {
				continue;
			}

			// not a menu sentence if it has plain text in it
			// though! we have to make this exception to stop
			// stuff like
			// "Wedding Ceremonies, No preservatives, more... "
			// from switchboard.com from being a menu sentence
			// just because "more" is in a link.
			if ( sk->m_flags & SEC_PLAIN_TEXT ) {
				break;
			}

			// set it
			sk->m_flags |= SEC_MENU_SENTENCE;

			// and stop
			break;
		}
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
		}
	}

	// strange?
	if ( ! sr ) { g_process.shutdownAbort(true); }
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
	}
}


// . set SEC_HEADING bits in Section::m_flags
// . identifies sections that are most likely headings
// . the WHOLE idea of this algo is to take a list of sections that are all 
//   the same tagId/baseHash and differentiate them so we can insert implied 
//   sections with headers. 
bool Sections::setHeadingBit ( ) {

	int32_t headings = 0;
	// scan the sections
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
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
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// these have to be in order of sn->m_a to work right
		// because we rely on the parent tag hash, which would not
		// necessarily be set if we were not sorted, because the
		// parent section could have SEC_FAKE flag set because it is
		// a br section added afterwards.

		// shortcut
		int64_t bh = (int64_t)sn->m_baseHash;

		// sanity check
		if ( bh == 0 ) { g_process.shutdownAbort(true); }

		// if no parent, use initial values
		if ( ! sn->m_parent ) {
			sn->m_depth   = 0;
			sn->m_tagHash = bh;

			// sanity check
			if ( bh == 0 ) { g_process.shutdownAbort(true); }
			continue;
		}

		// sanity check
		if ( sn->m_parent->m_tagHash == 0 ) { g_process.shutdownAbort(true); }

		// . update the cumulative front tag hash
		// . do not include hyperlinks as part of the cumulative hash!
		sn->m_tagHash = hash32h ( bh , sn->m_parent->m_tagHash );

		sn->m_colorHash = hash32h ( bh , sn->m_parent->m_colorHash );

		// if we are an implied section, just use the tag hash of
		// our parent. that way since we add different implied
		// sections for msichicago.com root than we do the kid,
		// the section voting should still match up
		if ( bh == BH_IMPLIED ) {
			sn->m_tagHash     = sn->m_parent->m_tagHash;
		}

		if ( sn->m_tagHash == 0 ) {
			sn->m_tagHash = 1234567;
		}

		// depth based on parent, too
		sn->m_depth = sn->m_parent->m_depth + 1;
	}
}

// make this replace ::print() when it works
bool Sections::print( SafeBuf *sbuf, int32_t hiPos, int32_t *wposVec, char *densityVec, char *wordSpamVec, char *fragVec ) {
	// save ptrs
	m_sbuf = sbuf;

	m_sbuf->setLabel ("sectprnt");

	m_hiPos = hiPos;

	m_wposVec      = wposVec;
	m_densityVec   = densityVec;
	m_wordSpamVec  = wordSpamVec;
	m_fragVec      = fragVec;

	//verifySections();

	char  **wptrs = m_words->getWords    ();
	int32_t   *wlens = m_words->getWordLens ();
	int32_t    nw    = m_words->getNumWords ();

	// check words
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// get section
		Section *sn = m_sectionPtrs[i];
		if ( sn->m_a >  i ) { g_process.shutdownAbort(true); }
		if ( sn->m_b <= i ) { g_process.shutdownAbort(true); }
	}


	// print sections out
	for ( Section *sk = m_rootSection ; sk ; ) {
		// print this section
		printSectionDiv(sk);
		// advance
		int32_t b = sk->m_b;
		// stop if last
		if ( b >= m_nw ) break;
		// get section after that
		sk = m_sectionPtrs[b];
	}

	// print header
	const char *hdr =
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
		"<td><b>depth</b></td>"
		"<td><b>parent word range</b></td>"
		"<td><b>flags</b></td>"
		"<td><b>evIds</b></td>"
		"<td><b>text snippet</b></td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr);

	int32_t rcount = 0;
	int32_t scount = 0;
	// show word # of each section so we can look in PageParser.cpp's
	// output to see exactly where it starts, since we now label all
	// the words
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("</table>%s\n",hdr);
		const char *xs = "--";
		char ttt[100];
		if ( sn->m_contentHash64 ) {
			int32_t modified = sn->m_tagHash ^ sn->m_contentHash64;
			sprintf(ttt,"0x%" PRIx32,modified);
			xs = ttt;
		}
		// shortcut
		Section *parent = sn->m_parent;
		int32_t pswn = -1;
		int32_t pewn = -1;
		if ( parent ) pswn = parent->m_a;
		if ( parent ) pewn = parent->m_b;
		// print it
		sbuf->safePrintf("<tr><td>%" PRId32"</td>\n"
				 "<td>%" PRId32"</td>"
				 "<td>%" PRId32"</td>"
				 "<td>0x%" PRIx32"</td>"
				 "<td>0x%" PRIx32"</td>"
				 "<td>0x%" PRIx32"</td>"
				 "<td>0x%" PRIx32"</td>"
				 "<td>%s</td>"
				 "<td>%" PRId32"</td>"
				 "<td><nobr>%" PRId32" to %" PRId32"</nobr></td>"
				 "<td><nobr>" ,
				 scount++,
				 sn->m_a,
				 sn->m_b,
				 (int32_t)sn->m_baseHash,
				 (int32_t)sn->m_tagHash,
				 (int32_t)sn->m_contentHash64,
				 (int32_t)(sn->m_contentHash64^sn->m_tagHash),
				 xs,
				 sn->m_depth,
				 pswn,
				 pewn);
		// now show the flags
		printFlags ( sbuf , sn );
		// first few words of section
		int32_t a = sn->m_a;
		int32_t b = sn->m_b;
		// -1 means an unclosed tag!! should no longer be the case
		if ( b == -1 ) { g_process.shutdownAbort(true); }//b=m_words->m_numWords;
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

bool Sections::printSectionDiv( Section *sk ) {
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
			 "background-color:#%06" PRIx32";"
			 "margin-left:20px;"
			 "border:#%06" PRIx32" 1px solid;"
			 "color:#%06" PRIx32"\">",
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
	m_sbuf->safePrintf("A=%" PRId32" ",sk->m_a);

	// print tag hash now
	m_sbuf->safePrintf("taghash=%" PRIu32" ",(int32_t)sk->m_tagHash);

	if ( sk->m_contentHash64 )
		m_sbuf->safePrintf("ch64=%" PRIu64" ",sk->m_contentHash64);

	printFlags ( m_sbuf , sk );
	
	if ( isHardSection(sk) )
		m_sbuf->safePrintf("hardsec ");
	
	m_sbuf->safePrintf("</i>\n");

	// now print each word and subsections in this section
	int32_t a = sk->m_a;
	int32_t b = sk->m_b;
	for ( int32_t i = a ; i < b ; i++ ) {
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
			printSectionDiv(ws);
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
			m_sbuf->safePrintf("%" PRId32,m_wposVec[i]);
			if ( m_densityVec[i] != MAXDENSITYRANK )
				m_sbuf->safePrintf("/<font color=purple><b>%" PRId32
						   "</b></font>"
						   ,
						   (int32_t)m_densityVec[i]);

			if ( m_wordSpamVec[i] != MAXWORDSPAMRANK )
				m_sbuf->safePrintf("/<font color=red><b>%" PRId32
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
		if ( si->m_a >  i ) { g_process.shutdownAbort(true); }
		if ( si->m_b <= i ) { g_process.shutdownAbort(true); }
		// must have checksum
		if ( m_wids[i] && si->m_contentHash64==0){g_process.shutdownAbort(true);}
		// must have this set if 0
		if ( ! si->m_contentHash64 && !(si->m_flags & SEC_NOTEXT)) {
			g_process.shutdownAbort(true);}
		if (   si->m_contentHash64 &&  (si->m_flags & SEC_NOTEXT)) {
			g_process.shutdownAbort(true);}
	}

	// sanity check
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// get it
		//Section *sn = &m_sections[i];
		// get parent
		Section *sp = sn->m_parent;
	subloop3:
		// skip if no parent
		if ( ! sp ) continue;
		// make sure parent fully contains
		if ( sp->m_a > sn->m_a ) { g_process.shutdownAbort(true); }
		if ( sp->m_b < sn->m_b ) { g_process.shutdownAbort(true); }
		// and make sure every grandparent fully contains us too!
		sp = sp->m_parent;
		goto subloop3;
	}

	// sanity check
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		Section *sn = &m_sections[i];
		if ( sn->m_a >= sn->m_b ) { g_process.shutdownAbort(true); }
	}

	// sanity check, make sure each section is contained by the
	// smallest section containing it
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		for ( Section *sj = m_rootSection ; sj ; sj = sj->m_next ) {
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
			g_process.shutdownAbort(true);
		}
	}
	
	// make sure we map each word to a section that contains it at least
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		Section *si = m_sectionPtrs[i];
		if ( si->m_a >  i ) { g_process.shutdownAbort(true); }
		if ( si->m_b <= i ) { g_process.shutdownAbort(true); }
	}

	return true;
}
