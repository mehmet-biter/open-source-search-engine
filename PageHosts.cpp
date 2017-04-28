#include "gb-include.h"

#include "TcpSocket.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Hostdb.h"
#include "sort.h"
#include "Conf.h"
#include "ip.h"
#include "GbUtil.h"

static int defaultSort    ( const void *i1, const void *i2 );
static int pingSort1      ( const void *i1, const void *i2 );
static int pingSort2      ( const void *i1, const void *i2 );
static int pingAgeSort    ( const void *i1, const void *i2 );
static int pingMaxSort    ( const void *i1, const void *i2 );
static int splitTimeSort  ( const void *i1, const void *i2 );
static int flagSort       ( const void *i1, const void *i2 );
static int resendsSort    ( const void *i1, const void *i2 );
static int errorsSort     ( const void *i1, const void *i2 );
static int tryagainSort   ( const void *i1, const void *i2 );
static int dgramsToSort   ( const void *i1, const void *i2 );
static int dgramsFromSort ( const void *i1, const void *i2 );

static int32_t generatePingMsg( Host *h, int64_t nowms, char *buffer );

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageHosts ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	//char *p    = buf;
	//char *pend = buf + 64*1024;
	StackBuf<64*1024> sb;


	// XML OR JSON
	 char format = r->getReplyFormat();
	// if ( format == FORMAT_XML || format == FORMAT_JSON )
	// 	return sendPageHostsInXmlOrJson( s , r );


	// check for a sort request
	int32_t sort  = r->getLong ( "sort", -1 );
	// sort by hostid with dead on top by default
	if ( sort == -1 ) sort = 16;
	const char *coll = r->getString ( "c" );
	//char *pwd  = r->getString ( "pwd" );
	// check for setnote command
	int32_t setnote = r->getLong("setnote", 0);
	int32_t setsparenote = r->getLong("setsparenote", 0);
	// check for replace host command
	int32_t replaceHost = r->getLong("replacehost", 0);

	// set note...
	if ( setnote == 1 ) {
		// get the host id to change
		int32_t host = r->getLong("host", -1);
		if ( host == -1 ) goto skipReplaceHost;
		// get the note to set
		int32_t  noteLen;
		const char *note = r->getString("note", &noteLen, "", 0);
		// set the note
		g_hostdb.setNote(host, note, noteLen);
	}
	// set spare note...
	if ( setsparenote == 1 ) {
		// get the host id to change
		int32_t spare = r->getLong("spare", -1);
		if ( spare == -1 ) goto skipReplaceHost;
		// get the note to set
		int32_t  noteLen;
		const char *note = r->getString("note", &noteLen, "", 0);
		// set the note
		g_hostdb.setSpareNote(spare, note, noteLen);
	}
	// replace host...
	if ( replaceHost == 1 ) {
		// get the host ids to swap
		int32_t rhost = r->getLong("rhost", -1);
		int32_t rspare = r->getLong("rspare", -1);
		if ( rhost == -1 || rspare == -1 )
			goto skipReplaceHost;
		// replace
		g_hostdb.replaceHost(rhost, rspare);
	}

skipReplaceHost:

	int32_t refreshRate = r->getLong("rr", 0);
	if(refreshRate > 0 && format == FORMAT_HTML ) 
		sb.safePrintf("<META HTTP-EQUIV=\"refresh\" "
			      "content=\"%" PRId32"\"\\>",
			      refreshRate);

	// print standard header
	// 	char *pp    = sb.getBuf();
	// 	char *ppend = sb.getBufEnd();
	// 	if ( pp ) {
	if ( format == FORMAT_HTML ) g_pages.printAdminTop ( &sb , s , r );
	//	sb.incrementLength ( pp - sb.getBuf() );
	//	}
	const char *colspan = "30";
	//char *shotcol = "";
	char shotcol[1024];
	shotcol[0] = '\0';
	const char *cs = coll;
	if ( ! cs ) cs = "";

	if ( g_conf.m_useShotgun && format == FORMAT_HTML ) {
		colspan = "31";
		//shotcol = "<td><b>ip2</b></td>";
		sprintf ( shotcol, "<td><a href=\"/admin/hosts?c=%s"
			 	   "&sort=2\">"
			  "<b>ping2</b></td></a>",
			  cs);
	}

	// print host table
	if ( format == FORMAT_HTML )
		sb.safePrintf ( 
			       "<table %s>"
			       "<tr><td colspan=%s><center>"
			       "<b>Hosts "
			       "(<a href=\"/admin/hosts?c=%s&sort=%" PRId32"&resetstats=1\">"
			       "reset)</a></b>"
			       "</td></tr>" 
			       "<tr bgcolor=#%s>"
			       "<td><a href=\"/admin/hosts?c=%s&sort=0\">"

			       "<b>hostId</b></a></td>"
			       "<td><b>host ip</b></td>"
			       "<td><b>shard</b></td>"
			       "<td><b>mirror</b></td>" // mirror # within the shard

			       "<td><b>http port</b></td>"

			       "<td><b>GB version</b></td>"

			       "<td><a href=\"/admin/hosts?c=%s&sort=3\">"
			       "<b>dgrams resent</b></a></td>"

			       "<td><b>try agains recvd</b></td>"

			       "<td><a href=\"/admin/hosts?c=%s&sort=13\">"
			       "<b>avg split time</b></a></td>"

			       "<td><b>splits done</b></a></td>"

			       "<td><a href=\"/admin/hosts?c=%s&sort=12\">"
			       "<b>status</b></a></td>"

			       "<td><b>docs indexed</a></td>"

			       "<td><a href=\"/admin/hosts?c=%s&sort=14\">"
			       "<b>max ping1</b></a></td>"

			       "<td><a href=\"/admin/hosts?c=%s&sort=11\">"
			       "<b>ping1 age</b></a></td>"

			       //"<td><b>ip1</td>"
			       "<td><a href=\"/admin/hosts?c=%s&sort=1\">"
			       "<b>ping1</b></a></td>"

			       "%s"
			       "<td><b>note</b></td>",
			       TABLE_STYLE ,
			       colspan    ,

			       cs, sort,
			       DARK_BLUE  ,

			       cs,
			       cs,
			       cs,
			       cs,
			       cs,
			       cs,
			       cs,
			       shotcol    );

	// loop through each host we know and print it's stats
	int32_t nh = g_hostdb.getNumHosts();
	// should we reset resends, errorsRecvd and ETRYAGAINS recvd?
	if ( r->getLong("resetstats",0) ) {
		for ( int32_t i = 0 ; i < nh ; i++ ) {
			// get the ith host (hostId)
			Host *h = g_hostdb.getHost ( i );
			h->m_totalResends   = 0;
			h->m_errorReplies = 0;
			h->m_etryagains   = 0;
			h->m_dgramsTo     = 0;
			h->m_dgramsFrom   = 0;
			h->m_splitTimes = 0;
			h->m_splitsDone = 0;
		}
	}

	// sort hosts if needed
	int32_t hostSort [ MAX_HOSTS ];
	for ( int32_t i = 0 ; i < nh ; i++ )
		hostSort [ i ] = i;
	switch ( sort ) {
	case 1: gbsort ( hostSort, nh, sizeof(int32_t), pingSort1      ); break;
	case 2: gbsort ( hostSort, nh, sizeof(int32_t), pingSort2      ); break;
	case 3: gbsort ( hostSort, nh, sizeof(int32_t), resendsSort    ); break;
	case 4: gbsort ( hostSort, nh, sizeof(int32_t), errorsSort     ); break;
	case 5: gbsort ( hostSort, nh, sizeof(int32_t), tryagainSort   ); break;
	case 6: gbsort ( hostSort, nh, sizeof(int32_t), dgramsToSort   ); break;
	case 7: gbsort ( hostSort, nh, sizeof(int32_t), dgramsFromSort ); break;
	//case 8:
	case 11:gbsort ( hostSort, nh, sizeof(int32_t), pingAgeSort    ); break;
	case 12:gbsort ( hostSort, nh, sizeof(int32_t), flagSort       ); break;
	case 13:gbsort ( hostSort, nh, sizeof(int32_t), splitTimeSort  ); break;
	case 14:gbsort ( hostSort, nh, sizeof(int32_t), pingMaxSort    ); break;
	//case 15:
	case 16:gbsort ( hostSort, nh, sizeof(int32_t), defaultSort    ); break;

	}

	if ( format == FORMAT_XML ) {
		sb.safePrintf("<response>\n");
		sb.safePrintf("\t<statusCode>0</statusCode>\n");
		sb.safePrintf("\t<statusMsg>Success</statusMsg>\n");
	}

	if ( format == FORMAT_JSON ) {
		sb.safePrintf("{\"response\":{\n");
		sb.safePrintf("\t\"statusCode\":0,\n");
		sb.safePrintf("\t\"statusMsg\":\"Success\",\n");
	}

	int64_t nowmsLocal = gettimeofdayInMilliseconds();

	// compute majority gb version so we can highlight bad out of sync
	// gb versions in red below
	int32_t majorityHash32 = 0;
	int32_t lastCount = 0;
	// get majority gb version
	for ( int32_t si = 0 ; si < nh ; si++ ) {
		int32_t i = hostSort[si];
		// get the ith host (hostId)
		Host *h = g_hostdb.getHost ( i );
		char *vbuf = h->m_pingInfo.m_gbVersionStr;//gbVersionStrBuf;
		int32_t vhash32 = hash32n ( vbuf );
		if ( vhash32 == majorityHash32 ) lastCount++;
		else lastCount--;
		if ( lastCount < 0 ) majorityHash32 = vhash32;
	}


	if(format==FORMAT_JSON)
		sb.safePrintf("\t\"hosts\": [\n");
	
	//int32_t ng = g_hostdb.getNumGroups();
	for ( int32_t si = 0 ; si < nh ; si++ ) {
		int32_t i = hostSort[si];
		// get the ith host (hostId)
		Host *h = g_hostdb.getHost ( i );
		// get avg/stdDev msg roundtrip times in ms for ith host
		//int32_t avg , stdDev;
		//g_hostdb.getTimes ( i , &avg , &stdDev );
                char ptr[256];
                int32_t pingAge = generatePingMsg(h, nowmsLocal, ptr);
		char pms[64];
		if ( h->m_pingMax < 0 ) sprintf(pms,"???");
		else                    sprintf(pms,"%" PRId32"ms",h->m_pingMax);
		char ipbuf1[64];
		char ipbuf2[64];
		strcpy(ipbuf1,iptoa(h->m_ip));
		strcpy(ipbuf2,iptoa(h->m_ipShotgun));

		char *vbuf = h->m_pingInfo.m_gbVersionStr;//m_gbVersionStrBuf;
		// get hash
		int32_t vhash32 = hash32n ( vbuf );
		const char *vbuf1 = "";
		const char *vbuf2 = "";
		if ( vhash32 != majorityHash32 ) {
			vbuf1 = "<font color=red><b>";
			vbuf2 = "</font></b>";
		}

		//int32_t switchGroup = 0;
		//if ( g_hostdb.m_indexSplits > 1 )
		//	switchGroup = h->m_group%g_hostdb.m_indexSplits;

		// host can have 2 ip addresses, get the one most
		// similar to that of the requester
		int32_t eip = g_hostdb.getBestIp ( h );
		char ipbuf3[64];
		strcpy(ipbuf3,iptoa(eip));

		// split time, don't divide by zero!
		int32_t splitTime = 0;
		if ( h->m_splitsDone ) 
			splitTime = h->m_splitTimes / h->m_splitsDone;

		//char flagString[32];
		StackBuf<64> fb;
		//char *fs = flagString;
		//*fs = '\0';

		// does its hosts.conf file disagree with ours?
		if ( h->m_pingInfo.m_hostsConfCRC &&
		     format == FORMAT_HTML &&
		     h->m_pingInfo.m_hostsConfCRC != g_hostdb.getCRC() )
			fb.safePrintf("<font color=red><b title=\"Hosts.conf "
				      "in disagreement with ours.\">H"
				      "</b></font>");
		if ( h->m_pingInfo.m_hostsConfCRC &&
		     format != FORMAT_HTML &&
		     h->m_pingInfo.m_hostsConfCRC != g_hostdb.getCRC() )
			fb.safePrintf("Hosts.conf in disagreement with ours");

		int32_t flags = h->m_pingInfo.m_flags;


		if ( format == FORMAT_HTML ) {
			// use these new ones for now
			int n = h->m_pingInfo.m_numCorruptDiskReads;
			if ( n )
				fb.safePrintf("<font color=red><b>"
					      "C"
					      "<sup>%" PRId32"</sup>"
					      "</b></font>"
					      , n );
			n = h->m_pingInfo.m_socketsClosedFromHittingLimit;
			if ( n )
				fb.safePrintf("<font color=red><b>"
					      "K"
					      "<sup>%" PRId32"</sup>"
					      "</b></font>"
					      , n );
		}

		// recovery mode? reocvered from coring?
		if ((flags & PFLAG_RECOVERYMODE)&& format == FORMAT_HTML ) {
			fb.safePrintf("<b title=\"Recovered from core"
				      "\">x</b>");
		}

		if ((flags & PFLAG_RECOVERYMODE)&& format != FORMAT_HTML )
			fb.safePrintf("Recovered from core");

		// rebalancing?
		if ( (flags & PFLAG_REBALANCING)&& format == FORMAT_HTML )
			fb.safePrintf("<b title=\"Currently "
				      "rebalancing\">R</b>");
		if ( (flags & PFLAG_REBALANCING)&& format != FORMAT_HTML )
			fb.safePrintf("Currently rebalancing");

		// has recs that should be in another shard? indicates
		// we need to rebalance or there is a bad hosts.conf
		if ((flags & PFLAG_FOREIGNRECS) && format == FORMAT_HTML )
			fb.safePrintf("<font color=red><b title=\"Foreign "
				      "data "
				      "detected. Needs rebalance.\">F"
				      "</b></font>");
		if ((flags & PFLAG_FOREIGNRECS) && format != FORMAT_HTML )
			fb.safePrintf("Foreign data detected. "
				      "Needs rebalance.");

		// if it has spiders going on say "S" with # as the superscript
		if ((flags & PFLAG_HASSPIDERS) && format == FORMAT_HTML )
			fb.safePrintf ( "<span title=\"Spidering\">S"
					"<sup>%" PRId32"</sup>"
					"</span>"
					,h->m_pingInfo.m_currentSpiders
					);

		if ( format == FORMAT_HTML && 
		     h->m_pingInfo.m_udpSlotsInUseIncoming ) {
			const char *f1 = "";
			const char *f2 = "";
			// MAXUDPSLOTS in Spider.cpp is 300 right now
			if ( h->m_pingInfo.m_udpSlotsInUseIncoming >= 300 ) {
				f1 = "<b>";
				f2 = "</b>";
			}
			if ( h->m_pingInfo.m_udpSlotsInUseIncoming >= 400 ) {
				f1 = "<b><font color=red>";
				f2 = "</font></b>";
			}
			fb.safePrintf("<span title=\"udpSlotsInUse\">"
				      "%s"
				      "U"
				      "<sup>%" PRId32"</sup>"
				      "%s"
				      "</span>"
				      ,f1
				      ,h->m_pingInfo.m_udpSlotsInUseIncoming
				      ,f2
				      );
		}

		if ( format == FORMAT_HTML && h->m_pingInfo.m_tcpSocketsInUse){
			const char *f1 = "";
			const char *f2 = "";
			if ( h->m_pingInfo.m_tcpSocketsInUse >= 100 ) {
				f1 = "<b>";
				f2 = "</b>";
			}
			if ( h->m_pingInfo.m_tcpSocketsInUse >= 200 ) {
				f1 = "<b><font color=red>";
				f2 = "</font></b>";
			}
			fb.safePrintf("<span title=\"tcpSocketsInUse\">"
				      "%s"
				      "T"
				      "<sup>%" PRId32"</sup>"
				      "%s"
				      "</span>"
				      ,f1
				      ,h->m_pingInfo.m_tcpSocketsInUse
				      ,f2
				      );
		}

		if ((flags & PFLAG_HASSPIDERS) && format != FORMAT_HTML )
			fb.safePrintf ( "Spidering");

		// say "M" if merging
		if ( (flags & PFLAG_MERGING) && format == FORMAT_HTML )
			fb.safePrintf ( "<span title=\"Merging\">M</span>");
		if ( (flags & PFLAG_MERGING) && format != FORMAT_HTML )
			fb.safePrintf ( "Merging");

		// say "D" if dumping
		if (   (flags & PFLAG_DUMPING) && format == FORMAT_HTML )
			fb.safePrintf ( "<span title=\"Dumping\">D</span>");
		if (   (flags & PFLAG_DUMPING) && format != FORMAT_HTML )
			fb.safePrintf ( "Dumping");


		// say "y" if doing the daily merge
		if (  !(flags & PFLAG_MERGEMODE0) )
			fb.safePrintf ( "y");


		if(format == FORMAT_HTML) {
			if(!h->m_spiderEnabled)
				fb.safePrintf("<span title=\"Spider Disabled\" style=\"text-decoration:line-through;\">S</span>");
			if(!h->m_queryEnabled)
				fb.safePrintf("<span title=\"Query Disabled\" style=\"text-decoration:line-through;\">Q</span>");
		}


		// clear it if it is us, this is invalid
		if ( ! h->m_gotPingReply ) {
			fb.reset();
			fb.safePrintf("??");
		}
		if ( fb.length() == 0 && format == FORMAT_HTML )
			fb.safePrintf("&nbsp;");

		fb.nullTerm();

		const char *bg = LIGHT_BLUE;
		if ( h->m_ping >= g_conf.m_deadHostTimeout ) 
			bg = "ffa6a6";


		//
		// BEGIN XML OUTPUT
		//
		if ( format == FORMAT_XML ) {
			
			sb.safePrintf("\t<host>\n"
				      "\t\t<name><![CDATA["
				      );
			cdataEncode(&sb, h->m_hostname);
			sb.safePrintf("]]></name>\n");
			sb.safePrintf("\t\t<shard>%" PRId32"</shard>\n",
				      (int32_t)h->m_shardNum);
			sb.safePrintf("\t\t<mirror>%" PRId32"</mirror>\n",
				      h->m_stripe);

			sb.safePrintf("\t\t<ip1>%s</ip1>\n",
				      iptoa(h->m_ip));
			sb.safePrintf("\t\t<ip2>%s</ip2>\n",
				      iptoa(h->m_ipShotgun));

			sb.safePrintf("\t\t<httpPort>%" PRId32"</httpPort>\n",
				      (int32_t)h->getInternalHttpPort());
			sb.safePrintf("\t\t<udpPort>%" PRId32"</udpPort>\n",
				      (int32_t)h->m_port);
			sb.safePrintf("\t\t<dnsPort>%" PRId32"</dnsPort>\n",
				      (int32_t)h->m_dnsClientPort);

			sb.safePrintf("\t\t<gbVersion>%s</gbVersion>\n",vbuf);

			sb.safePrintf("\t\t<resends>%" PRId32"</resends>\n",
				      h->m_totalResends.load());

			/*
			  MDW: take out for new stuff
			sb.safePrintf("\t\t<errorReplies>%" PRId32"</errorReplies>\n",
				      h->m_errorReplies);
			*/

			sb.safePrintf("\t\t<errorTryAgains>%" PRId32
				      "</errorTryAgains>\n",
				      h->m_etryagains.load());

			sb.safePrintf("\t\t<udpSlotsInUse>%" PRId32
				      "</udpSlotsInUse>\n",
				      h->m_pingInfo.m_udpSlotsInUseIncoming);

			sb.safePrintf("\t\t<tcpSocketsInUse>%" PRId32
				      "</tcpSocketsInUse>\n",
				      h->m_pingInfo.m_tcpSocketsInUse);

			/*
			sb.safePrintf("\t\t<dgramsTo>%" PRId64"</dgramsTo>\n",
				      h->m_dgramsTo);
			sb.safePrintf("\t\t<dgramsFrom>%" PRId64"</dgramsFrom>\n",
				      h->m_dgramsFrom);
			*/

			sb.safePrintf("\t\t<numCorruptDiskReads>%" PRId32
				      "</numCorruptDiskReads>\n"
				      ,h->m_pingInfo.m_numCorruptDiskReads);
			sb.safePrintf("\t\t<numClosedSockets>%" PRId32
				      "</numClosedSockets>\n"
				      ,h->m_pingInfo.
				      m_socketsClosedFromHittingLimit);
			sb.safePrintf("\t\t<numOutstandingSpiders>%" PRId32
				      "</numOutstandingSpiders>\n"
				      ,h->m_pingInfo.m_currentSpiders );


			sb.safePrintf("\t\t<splitTime>%" PRId32"</splitTime>\n",
				      splitTime);
			sb.safePrintf("\t\t<splitsDone>%" PRId32"</splitsDone>\n",
				      h->m_splitsDone);
			
			sb.safePrintf("\t\t<status><![CDATA[%s]]></status>\n",
				      fb.getBufStart());

			sb.safePrintf("\t\t<docsIndexed>%" PRId32
				      "</docsIndexed>\n",
				      h->m_pingInfo.m_totalDocsIndexed);

			sb.safePrintf("\t\t<maxPing1>%s</maxPing1>\n",
				      pms );

			sb.safePrintf("\t\t<maxPingAge1>%" PRId32"ms</maxPingAge1>\n",
				      pingAge );

			sb.safePrintf("\t\t<ping1>%s</ping1>\n",
				      ptr );

			sb.safePrintf("\t\t<note>%s</note>\n",
				      h->m_note );

			sb.safePrintf("\t\t<spider>%" PRId32"</spider>\n",
						  (int32_t)h->m_spiderEnabled );


			sb.safePrintf("\t\t<query>%" PRId32"</query>\n",
						  (int32_t)h->m_queryEnabled );

			sb.safePrintf("\t</host>\n");

			continue;
		}
		//
		// END XML OUTPUT
		//


		//
		// BEGIN JSON OUTPUT
		//
		if ( format == FORMAT_JSON ) {
			
			sb.safePrintf("\t\t\t{\n");
			sb.safePrintf("\t\t\t\t\"name\":\"%s\",\n",h->m_hostname);
			sb.safePrintf("\t\t\t\t\"shard\":%" PRId32",\n",
				      (int32_t)h->m_shardNum);
			sb.safePrintf("\t\t\t\t\"mirror\":%" PRId32",\n", h->m_stripe);

			sb.safePrintf("\t\t\t\t\"ip1\":\"%s\",\n",iptoa(h->m_ip));
			sb.safePrintf("\t\t\t\t\"ip2\":\"%s\",\n",
				      iptoa(h->m_ipShotgun));

			sb.safePrintf("\t\t\t\t\"httpPort\":%" PRId32",\n",
				      (int32_t)h->getInternalHttpPort());
			sb.safePrintf("\t\t\t\t\"udpPort\":%" PRId32",\n",
				      (int32_t)h->m_port);
			sb.safePrintf("\t\t\t\t\"dnsPort\":%" PRId32",\n",
				      (int32_t)h->m_dnsClientPort);

			sb.safePrintf("\t\t\t\t\"gbVersion\":\"%s\",\n",vbuf);

			sb.safePrintf("\t\t\t\t\"resends\":%" PRId32",\n",
				      h->m_totalResends.load());

			/*
			sb.safePrintf("\t\t\t\t\"errorReplies\":%" PRId32",\n",
				      h->m_errorReplies);
			*/
			sb.safePrintf("\t\t\t\t\"errorTryAgains\":%" PRId32",\n",
				      h->m_etryagains.load());
			sb.safePrintf("\t\t\t\t\"udpSlotsInUse\":%" PRId32",\n",
				      h->m_pingInfo.m_udpSlotsInUseIncoming);
			sb.safePrintf("\t\t\t\t\"tcpSocketsInUse\":%" PRId32",\n",
				      h->m_pingInfo.m_tcpSocketsInUse);

			/*
			sb.safePrintf("\t\t\t\t\"dgramsTo\":%" PRId64",\n",
				      h->m_dgramsTo);
			sb.safePrintf("\t\t\t\t\"dgramsFrom\":%" PRId64",\n",
				      h->m_dgramsFrom);
			*/


			sb.safePrintf("\t\t\t\t\"numCorruptDiskReads\":%" PRId32",\n"
				      ,h->m_pingInfo.m_numCorruptDiskReads);
			sb.safePrintf("\t\t\t\t\"numClosedSockets\":%" PRId32",\n"
				      ,h->m_pingInfo.
				      m_socketsClosedFromHittingLimit);
			sb.safePrintf("\t\t\t\t\"numOutstandingSpiders\":%" PRId32
				      ",\n"
				      ,h->m_pingInfo.m_currentSpiders );


			sb.safePrintf("\t\t\t\t\"splitTime\":%" PRId32",\n",
				      splitTime);
			sb.safePrintf("\t\t\t\t\"splitsDone\":%" PRId32",\n",
				      h->m_splitsDone);
			
			sb.safePrintf("\t\t\t\t\"status\":\"%s\",\n",
				      fb.getBufStart());

			sb.safePrintf("\t\t\t\t\"docsIndexed\":%" PRId32",\n",
				      h->m_pingInfo.m_totalDocsIndexed);

			sb.safePrintf("\t\t\t\t\"maxPing1\":\"%s\",\n",pms);

			sb.safePrintf("\t\t\t\t\"maxPingAge1\":\"%" PRId32"ms\",\n",
				      pingAge );

			sb.safePrintf("\t\t\t\t\"ping1\":\"%s\",\n",
				      ptr );

			sb.safePrintf("\t\t\t\t\"note\":\"%s\",\n",
				      h->m_note );

			sb.safePrintf("\t\t\t\t\"spider\":\"%" PRId32"\",\n",
						  (int32_t)h->m_spiderEnabled );

			sb.safePrintf("\t\t\t\t\"query\":\"%" PRId32"\"\n",
						  (int32_t)h->m_queryEnabled );


			sb.safePrintf("\t\t\t}");
			if(si+1 < nh)
				sb.safePrintf(",");
			sb.safePrintf("\n");

			continue;
		}
		//
		// END JSON OUTPUT
		//


		sb.safePrintf (
			  "<tr bgcolor=#%s>"
			  "<td><a href=\"http://%s:%hi/admin/hosts?"
			  ""
			  "c=%s"
			  "&sort=%" PRId32"\">%" PRId32"</a></td>"

			  "<td>%s</td>" // hostname

			  "<td>%" PRId32"</td>" // group
			  "<td>%" PRId32"</td>" // stripe
			  //"<td>0x%08" PRIx32"</td>" // group mask

			  //"<td>%s</td>" // ip1
			  //"<td>%s</td>" // ip2
			  //"<td>%hi</td>" // port
			  //"<td>%hi</td>" // client port
			  "<td>%hi</td>" // http port
			  //"<td>%" PRId32"</td>" // token group num
			  //"<td>%" PRId32"</td>" // switch group
			  //"<td>%s</td>" // tmpN

			  // hd temps
			  // no, this is gb version now
			  "<td><nobr>%s%s%s</nobr></td>"

			  // resends
			  "<td>%" PRId32"</td>"

			  // error replies
			  //"<td>%" PRId32"</td>"

			  // etryagains
			  "<td>%" PRId32"</td>"

			  // # dgrams sent to
			  //"<td>%" PRId64"</td>"
			  // # dgrams recvd from
			  //"<td>%" PRId64"</td>"

			  // loadavg
			  //"<td>%.2f</td>"

			  // split time
			  "<td>%" PRId32"</td>"
			  // splits done
			  "<td>%" PRId32"</td>"

			  // flags
			  "<td>%s</td>"

			  // docs indexed
			  "<td>%" PRId32"</td>"

			  // ping max
			  "<td>%s</td>"

			  // ping age
			  "<td>%" PRId32"ms</td>"

			  // ping
			  "<td>%s</td>"
			  "<td nowrap=1>%s</td>"
			  "</tr>" , 
			  bg,//LIGHT_BLUE ,
			  ipbuf3, h->getInternalHttpPort(),
			  cs, sort,
			  i , 
			  h->m_hostname,
			  (int32_t)h->m_shardNum,//group,
			  h->m_stripe,
			  h->getInternalHttpPort(),
			  vbuf1,
			  vbuf,//hdbuf,
			  vbuf2,

			  h->m_totalResends.load(),


			  // h->m_errorReplies,
			  h->m_etryagains.load(),
			  // h->m_dgramsTo,
			  // h->m_dgramsFrom,

			  //h->m_loadAvg, // double
			  splitTime,
			  h->m_splitsDone,

			  fb.getBufStart(),//flagString,

			  h->m_pingInfo.m_totalDocsIndexed,

			  // ping max
			  pms,
			  // ping age
			  pingAge,

			  //avg , 
			  //stdDev,
			  //ping,
			  ptr ,
			  //ptr2 ,
			  h->m_note );
	}

	if ( format == FORMAT_XML ) {
		sb.safePrintf("</response>\n");
		return g_httpServer.sendDynamicPage ( s , 
						      sb.getBufStart(),
						      sb.length() ,
						      0, 
						      false, 
						      "text/xml");
	}

	if ( format == FORMAT_JSON ) {
		sb.safePrintf("\t\t]");
		sb.safePrintf("\n}");
		sb.safePrintf("}");
		return g_httpServer.sendDynamicPage ( s , 
						      sb.getBufStart(),
						      sb.length() ,
						      0, 
						      false, 
						      "application/json");
	}


	// end the table now
	sb.safePrintf ( "</table><br>\n" );


	if( g_hostdb.m_numSpareHosts ) {
		// print spare hosts table
		sb.safePrintf ( 
					   "<table %s>"
					   "<tr class=hdrow><td colspan=10><center>"
					   //"<font size=+1>"
					   "<b>Spares</b>"
					   //"</font>"
					   "</td></tr>" 
					   "<tr bgcolor=#%s>"
					   "<td><b>spareId</td>"
					   "<td><b>host name</td>"
					   "<td><b>ip1</td>"
					   "<td><b>ip2</td>"
					   //"<td><b>udp port</td>"
					   //"<td><b>priority udp port</td>"
					   //"<td><b>dns client port</td>"
					   "<td><b>http port</td>"
					   //"<td><b>switch id</td>"

					   // this is now fairly obsolete
					   //"<td><b>ide channel</td>"

					   "<td><b>note</td>",
					   TABLE_STYLE,
					   DARK_BLUE  );

		for ( int32_t i = 0; i < g_hostdb.m_numSpareHosts; i++ ) {
			// get the ith host (hostId)
			Host *h = g_hostdb.getSpare ( i );

			char ipbuf1[64];
			char ipbuf2[64];
			strcpy(ipbuf1,iptoa(h->m_ip));
			strcpy(ipbuf2,iptoa(h->m_ipShotgun));

			// print it
			sb.safePrintf (
						   "<tr bgcolor=#%s>"
						   "<td>%" PRId32"</td>"
						   "<td>%s</td>"
						   "<td>%s</td>"
						   "<td>%s</td>"
						   //"<td>%hi</td>"
						   //"<td>%hi</td>" // priority udp port
						   //"<td>%hi</td>"
						   "<td>%hi</td>"
						   //"<td>%i</td>" // switch id
						   "<td>%s</td>"
						   "</tr>" , 
						   LIGHT_BLUE,
						   i , 
						   h->m_hostname,
						   ipbuf1,
						   ipbuf2,
						   //h->m_port , 
						   //h->m_port2 , 
						   //h->m_dnsClientPort ,
						   h->getInternalHttpPort(),
						   //h->m_switchId,
						   h->m_note );
		}
		sb.safePrintf ( "</table><br>" );
	}

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );


	// print help table
	sb.safePrintf ( 
		  "<table %s>"
		  "<tr class=hdrow><td colspan=10><center>"
		  //"<font size=+1>"
		  "<b>Key</b>"
		  //"</font>"
		  "</td></tr>" 

		  "<tr class=poo>"
		  "<td>host ip</td>"
		  "<td>The primary IP address of the host."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>shard</td>"
		  "<td>"
		  "The index is split into shards. Which shard does this "
		  "host serve?"
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>mirror</td>"
		  "<td>"
		  "A shard can be mirrored multiple times for "
		  "data redundancy."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>dgrams resent</td>"
		  "<td>How many datagrams have had to be resent to a host "
		  "because it was not ACKed quick enough or because it was "
		  "fully ACKed but the entire request was resent in case "
		  "the host was reset."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>try agains recvd</td>"
		  "<td>How many ETRYAGAIN errors "
		  "were received in response to a "
		  "request to add data. Usually because the host's memory "
		  "is full and it is dumping its data to disk. This number "
		  "can be high if the host if failing to dump the data "
		  "to disk because of some malfunction, and it can therefore "
		  "bottleneck the entire cluster."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>avg split time</td>"
		  "<td>Average time this host took to compute the docids "
		  "for a query. Useful for guaging the slowness of a host "
		  "compare to other hosts."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>splits done</td>"
		  "<td>Number of queries this host completed. Used in "
		  "computation of the <i>avg split time</i>."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>status</td>"
		  "<td>Status flags for the host. See key below."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>slow reads</td>"
		  "<td>Number of slow disk reads the host has had. "
		  "When this is big compared to other hosts it is a good "
		  "indicator its drives are relatively slow."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>docs indexed</td>"
		  "<td>Number of documents this host has indexed over all "
		  "collections. All hosts should have close to the same "
		  "number in a well-sharded situation."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>mem used</td>"
		  "<td>Percentage of memory currently used."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>cpu used</td>"
		  "<td>Percentage of cpu resources in use by the gb process."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>disk used</td>"
		  "<td>Percentage of disk in use. When this gets close to "
		  "100%% you need to do something."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>max ping1</td>"
		  "<td>The worst ping latency from host to host."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>ping1 age</td>"
		  "<td>How long ago the last ping request was sent to "
		  "this host. Let's us know how fresh the ping time is."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>ping1</td>"
		  "<td>Ping time to this host on the primary network."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>M (status flag)</td>"
		  "<td>Indicates host is merging files on disk."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>D (status flag)</td>"
		  "<td>Indicates host is dumping data to disk."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>S (status flag)</td>"
		  "<td>Indicates host has outstanding spiders."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>y (status flag)</td>"
		  "<td>Indicates host is performing the daily merge."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>R (status flag)</td>"
		  "<td>Indicates host is performing a rebalance operation."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>F (status flag)</td>"
		  "<td>Indicates host has foreign records and requires "
		  "a rebalance operation."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>x (status flag)</td>"
		  "<td>Indicates host has abruptly exited due to a fatal "
		  "error (cored) and "
		  "restarted itself. The exponent is how many times it has "
		  "done this. If no exponent, it only did it once."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>C (status flag)</td>"
		  "<td>Indicates # of corrupted disk reads."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td>K (status flag)</td>"
		  "<td>Indicates # of sockets closed from hitting limit."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td><nobr>O (status flag)</nobr></td>"
		  "<td>Indicates # of times we ran out of memory."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td><nobr>N (status flag)</nobr></td>"
		  "<td>Indicates host's clock is NOT in sync with host #0. "
		  "Gigablast should automatically sync on startup, "
		  "so this would be a problem "
		  "if it does not go away. Hosts need to have their clocks "
		  "in sync before they can add data to their index."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td><nobr>U (status flag)</nobr></td>"
		  "<td>Indicates the number of active UDP transactions "
		  "which are incoming requests. These will pile up if a "
		  "host can't handle them fast enough."
		  "</td>"
		  "</tr>\n"

		  "<tr class=poo>"
		  "<td><nobr>T (status flag)</nobr></td>"
		  "<td>Indicates the number of active TCP transactions "
		  "which are either outgoing or incoming requests."
		  "</td>"
		  "</tr>\n"

		  ,
		  TABLE_STYLE
			);

	sb.safePrintf ( "</table><br></form><br>" );

	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s , (char*) sb.getBufStart() ,
						  sb.length() );
}

static int32_t generatePingMsg( Host *h, int64_t nowms, char *buf ) {
        int32_t ping = h->m_ping;
        // show ping age first
        int32_t pingAge = nowms- h->m_lastPing;
        // if host is us, we don't ping ourselves
        if ( h->m_hostId == g_hostdb.m_hostId && h == g_hostdb.m_myHost)
                pingAge = 0;
        // if last ping is still 0, we haven't pinged it yet
        if ( h->m_lastPing == 0 ) pingAge = 0;
        // ping to string
        sprintf ( buf , "%" PRId32"ms", ping );
        // ping time ptr
        // make it "DEAD" if > 6000
        if ( ping >= g_conf.m_deadHostTimeout ) {
            sprintf(buf, "<font color=#ff0000><b>DEAD</b></font>");
        }

	if ( ! g_conf.m_useShotgun ) return pingAge;

	char *p = buf + strlen(buf);

	p += sprintf ( p , "</td><td>" );

    // the second eth port, ip2, the shotgun port
    int32_t pingB = h->m_pingShotgun;
    sprintf ( p , "%" PRId32"ms", pingB );
    if ( pingB >= g_conf.m_deadHostTimeout ) {
        sprintf(p,"<font color=#ff0000><b>DEAD</b></font>");
		return pingAge;
    }

    return pingAge;
}

int defaultSort   ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );

	if ( g_hostdb.isDead(h1) && ! g_hostdb.isDead(h2) ) return -1;
	if ( g_hostdb.isDead(h2) && ! g_hostdb.isDead(h1) ) return  1;

	if ( h1->m_hostId < h2->m_hostId ) return -1;
	return 1;
}

int pingSort1    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_ping > h2->m_ping ) return -1;
	if ( h1->m_ping < h2->m_ping ) return  1;
	return 0;
}

int pingSort2    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_pingShotgun > h2->m_pingShotgun ) return -1;
	if ( h1->m_pingShotgun < h2->m_pingShotgun ) return  1;
	return 0;
}

int pingMaxSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_pingMax > h2->m_pingMax ) return -1;
	if ( h1->m_pingMax < h2->m_pingMax ) return  1;
	return 0;
}

int pingAgeSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	//PingInfo *p1 = &h1->m_pingInfo;
	//PingInfo *p2 = &h2->m_pingInfo;
	if ( h1->m_lastPing > h2->m_lastPing ) return -1;
	if ( h1->m_lastPing < h2->m_lastPing ) return  1;
	return 0;
}

int splitTimeSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	int32_t t1 = 0;
	int32_t t2 = 0;
	if ( h1->m_splitsDone > 0 ) t1 = h1->m_splitTimes / h1->m_splitsDone;
	if ( h2->m_splitsDone > 0 ) t2 = h2->m_splitTimes / h2->m_splitsDone;
	if ( t1 > t2 ) return -1;
	if ( t1 < t2 ) return  1;
	return 0;
}

int flagSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	PingInfo *p1 = &h1->m_pingInfo;
	PingInfo *p2 = &h2->m_pingInfo;
	if ( p1->m_flags > p2->m_flags ) return -1;
	if ( p1->m_flags < p2->m_flags ) return  1;
	return 0;
}

int resendsSort  ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_totalResends > h2->m_totalResends )
		return -1;
	if ( h1->m_totalResends < h2->m_totalResends )
		return  1;
	return 0;
}

int errorsSort   ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_errorReplies > h2->m_errorReplies ) return -1;
	if ( h1->m_errorReplies < h2->m_errorReplies ) return  1;
	return 0;
}

int tryagainSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_etryagains>h2->m_etryagains)return -1;
	if ( h1->m_etryagains<h2->m_etryagains)return  1;
	return 0;
}

int dgramsToSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_dgramsTo > h2->m_dgramsTo ) return -1;
	if ( h1->m_dgramsTo < h2->m_dgramsTo ) return  1;
	return 0;
}


int dgramsFromSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(int32_t*)i1 );
	Host *h2 = g_hostdb.getHost ( *(int32_t*)i2 );
	if ( h1->m_dgramsFrom > h2->m_dgramsFrom ) return -1;
	if ( h1->m_dgramsFrom < h2->m_dgramsFrom ) return  1;
	return 0;
}
