#include "Robots.h"
#include "Url.h"
#include "Mime.h"
#include "fctypes.h"
#include "Mem.h" // gbstrlen

// taken from Robotdb.cpp
bool Robots::isAllowed ( Url   *url            ,
		  char  *userAgent      ,
		  char  *file           ,
		  int32_t   fileLen        ,
		  bool  *userAgentFound ,
		  bool   substringMatch ,
		  int32_t  *crawlDelay     ,
		  char **cacheStart     ,
		  int32_t  *cacheLen       ,
		  bool  *hadAllowOrDisallow ) {
	// assume nothing to cache yet
	*cacheLen   = 0;
	*cacheStart = file;
	// assume user agent is not in the file
	*userAgentFound = false;
	*hadAllowOrDisallow = false;
	// assume no crawl delay (-1)
	// *crawlDelay = -1;
	// if fileLen is 0 it is allowed
	if ( fileLen <= 0 ) return true;
	// get path from url, include cgi stuff
	char *path    = url->getPath();
	int32_t  pathLen = url->getPathLenWithCgi();
	// set the Mime class to this Mime file
	Mime mime;
	mime.set ( file , fileLen );
	// get a line of Mime
	char *f , *v;
	int32_t flen, vlen;
	// user agent length
	int32_t uaLen = gbstrlen (userAgent);
	// ptr into "file"
	char *p = file;
	char flag;
	bool allowed = true;
 loop:
	// if p is NULL now we're done
	if ( ! p ) return allowed;
	// get the next Mime line
	p = mime.getLine ( p , &f , &flen , &v , &vlen );
	// if this field is NOT "user-agent" skip it
	if ( flen != 10 ) goto loop;
	if ( strncasecmp ( f , "user-agent" , 10 ) != 0 ) goto loop;
 gotAgent:
	//some webmasters put comments at the end of their lines,
	//because they think this is a shell script or something.
	char* vv = v;
	while(vv - v < vlen && *vv != '#') vv++;
	vlen = vv - v;
	// decrement vlen to hack off spaces after the user-agent so that vlen
	// is really the length of the user agent
	while ( vlen > 0 && is_wspace_a(v[vlen-1]) ) vlen--;
	// now match the user agent
	if ( ! substringMatch && vlen != uaLen ) goto loop;
	// otherwise take the min of the lengths
	if ( uaLen < vlen ) vlen = uaLen;
	// is it the right user-agent?
	if ( strncasecmp ( v , userAgent , vlen ) != 0 ) goto loop;
	// we got it, if first instance start our cache here
	if ( !*userAgentFound ) *cacheStart = f;
	*userAgentFound = true;
	flag = 0;
 urlLoop:
	// if p is NULL now there is no more lines
	if ( ! p ) {
		// set our cache stop to the end of the file
		*cacheLen = (file + fileLen) - *cacheStart;
		return allowed;
	}
	// now loop over lines until we hit another user-agent line
	p = mime.getLine ( p , &f , &flen , &v , &vlen );
	// if it's another user-agent line ... ignore it unless we already
	// have seen a disallow line, in which case we got another set of
	if ( flag && flen==10 && strncasecmp(f,"user-agent",10)==0) {
		// set our cache stop here
		*cacheLen = f - *cacheStart;
		goto gotAgent;
	}
	// if a crawl delay, get the delay
	if ( flen == 11 && strncasecmp ( f , "crawl-delay", 11 ) == 0 ) {
		// set flag
		flag = 1;
		// skip if invalid. it could be ".5" seconds
		if ( ! is_digit ( *v ) && *v != '.' ) goto urlLoop;
		// get this. multiply crawl delay by x1000 to be in
		// milliseconds/ms
		int64_t vv = (int64_t)(atof(v) * 1000LL);
		// truncate to 0x7fffffff
		if      ( vv > 0x7fffffff ) *crawlDelay = 0x7fffffff;
		else if ( vv < 0          ) *crawlDelay = -1;
		else                        *crawlDelay = (int32_t)vv;
		// get the delay
		//*crawlDelay = atol(v) * 1000;
		goto urlLoop;
	}
	// if already disallowed, just goto the next line
	if ( !allowed ) goto urlLoop;
	// if we have an allow line or sitemap: line, then set flag to 1
	// so we can go to another user-agent line.
	// fixes romwebermarketplace.com/robots.txt
	// (doc.156447320458030317.txt)
	if ( flen==5 && strncasecmp(f,"allow"  ,5)==0 ) {
		*hadAllowOrDisallow = true;
		flag = 1;
	}
	if ( flen==7 && strncasecmp(f,"sitemap",7)==0 ) {
		flag = 1;
	}
	// if not disallow go to loop at top
	if ( flen != 8 ) goto urlLoop;
	if ( strncasecmp ( f , "disallow" , 8 ) != 0 ) {
		goto urlLoop;
	}
	// we had a disallow
	*hadAllowOrDisallow = true;
	// set flag
	flag = 1;
	// . take off trailing chars from the banned path name
	// . this is now done below
	//while ( vlen > 0 && is_space(v[vlen-1]) ) vlen--;
	// . skip leading spaces
	// . this should be done in mime class
	// while ( vlen > 0 && is_space(v[0]) ) { v++; vlen--; }
	// now stop at first space after url or end of line
	char *s    = v;
	char *send = v + vlen;
	// skip all non-space chars
	while ( s < send && ! is_wspace_a(*s) ) s++;
	// stop there
	vlen = s - v;
	// check for match
	char *tmpPath = path;
	int32_t tmpPathLen = pathLen;
	// assume path begins with /
	if ( vlen > 0 && v[0] != '/'){tmpPath++;tmpPathLen--;}
	if ( vlen > tmpPathLen ) goto urlLoop;
	if ( strncasecmp(tmpPath,v,vlen) != 0 ) goto urlLoop;
	// an exact match
	if ( vlen == tmpPathLen ) {
		//return false;
		allowed = false;
		goto urlLoop;
	}
	// must be something
	if ( vlen <= 0 ) goto urlLoop;
	// "v" may or may not end in a /, it really should end in a / though
	if ( v[vlen-1] == '/' && tmpPath[vlen-1] == '/' ) {
		//return false;
		allowed = false;
		goto urlLoop;
	}
	if ( v[vlen-1] != '/' && tmpPath[vlen  ] == '/' ) {
		//return false;
		allowed = false;
		goto urlLoop;
	}
	// let's be stronger. just do the substring match. if the webmaster
	// does not want us splitting path or file names then they should end
	// all of their robots.txt entries in a '/'. this also fixes the
	// problem of the "Disallow: index.htm?" line.
	//return false;
	allowed = false;
	// get another url path
	goto urlLoop;
}
