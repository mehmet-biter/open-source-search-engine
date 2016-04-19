#include "gb-include.h"

#include "TcpServer.h"
#include "Pages.h"
#include "Threads.h"
#include "SafeBuf.h"
#include "Profiler.h"


bool sendPageThreads ( TcpSocket *s , HttpRequest *r ) {
	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	// 	char *ss = p.getBuf();
	// 	char *ssend = p.getBufEnd();
	g_pages.printAdminTop ( &p , s , r );
	//p.incrementLength(sss - ss);
	
	int64_t now = gettimeofdayInMilliseconds();

	//p.safePrintf("the sizes are %"INT32" %"INT32"", g_conf.m_medReadSize ,g_conf.m_smaReadSize );

	for ( int32_t i = 0 ; i < g_threads.getNumThreadQueues(); i++ ) {
		const ThreadQueue* q = g_threads.getThreadQueue(i);

		//if ( q->m_top <= 0 ) continue;



		// int32_t loActive = q->m_loLaunched - q->m_loReturned;
		// int32_t mdActive = q->m_mdLaunched - q->m_mdReturned;
		// int32_t hiActive = q->m_hiLaunched - q->m_hiReturned;
		// int32_t      total    = loActive + mdActive + hiActive;

		int32_t total = q->m_launched - q->m_returned;
		
		p.safePrintf ( "<table %s>"
			       "<tr class=hdrow><td colspan=\"11\">"
			       //"<center>"
				//"<font size=+1>"
				"<b>Thread Type: %s"
				// "  (low: %"INT32""
				// "  med: %"INT32""
				// "  high: %"INT32""
				" (launched: %"INT32" "
			       "returned: %"INT32" "
			       "total: %"INT32" maxpossibleout: %i)</td></tr>",
			       TABLE_STYLE,
				q->getThreadType(), 
				// loActive, mdActive, 
				// hiActive, 
			       (int32_t)q->m_launched,
			       (int32_t)q->m_returned,
			       total,
			       (int)g_conf.m_max_threads);


		p.safePrintf ("<tr bgcolor=#%s>"
			      "<td><b>Status</b></td>"
			      "<td><b>Niceness</b></td>"
			      "<td><b>Queued Time</b></td>"
			      "<td><b>Run Time</b></td>"
			      "<td><b>Wait for Cleanup</b></td>"
			      "<td><b>Time So Far</b></td>"
			      "<td><b>Callback</b></td>"
			      "<td><b>Routine</b></td>"
			      "<td><b>Bytes Done</b></td>"
			      "<td><b>Megabytes/Sec</b></td>"
			      "<td><b>Read|Write</b></td>"
			      "</tr>"
			      , LIGHT_BLUE
			      );

		for ( int32_t j = 0 ; j < q->m_maxEntries ; j++ ) {
			ThreadEntry *t = &q->m_entries[j];
			if(!t->m_isOccupied) continue;

			FileState *fs = (FileState *)t->m_state;
			bool diskThread = false;
			if(q->m_threadType == THREAD_TYPE_DISK && fs) 
				diskThread = true;

			// might have got pre-called from EDISKSTUCK
			if ( ! t->m_callback ) fs = NULL;

			p.safePrintf("<tr bgcolor=#%s>", DARK_BLUE ); 
			
			if(t->m_isDone) {
				p.safePrintf("<td><font color='red'><b>done</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>%"INT64"ms</td>", t->m_launchedTime - t->m_queuedTime); //queued
				p.safePrintf("<td>%"INT64"ms</td>", t->m_exitTime - t->m_launchedTime); //run time
				p.safePrintf("<td>%"INT64"ms</td>", now - t->m_exitTime); //cleanup
				p.safePrintf("<td>%"INT64"ms</td>", now - t->m_queuedTime); //total
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs) {
					int64_t took = (t->m_exitTime - t->m_launchedTime);
					char *sign = "";
					if(took <= 0) {sign=">";took = 1;}
					p.safePrintf("<td>%"INT32"/%"INT32""
						     "</td>", 
						     t->m_bytesToGo, 
						     t->m_bytesToGo);
					p.safePrintf("<td>%s%.2f MB/s</td>", 
						     sign,
						     (float)t->m_bytesToGo/
						     (1024.0*1024.0)/
						     ((float)took/1000.0));
					p.safePrintf("<td>%s</td>",
						     t->m_doWrite? 
						     "<font color=red>"
						     "Write</font>":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			else if(t->m_isLaunched) {
				p.safePrintf("<td><font color='red'><b>running</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>%"INT64"</td>", t->m_launchedTime - t->m_queuedTime);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%"INT64"</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs ) {
					int64_t took = (now - t->m_launchedTime);
					if(took <= 0) took = 1;
					p.safePrintf("<td>%c%c%c/%"INT32"</td>", '?','?','?',t->m_bytesToGo);
					p.safePrintf("<td>%.2f MB/s</td>", 0.0);//(float)fs->m_bytesDone/took);
					p.safePrintf("<td>%s</td>",t->m_doWrite? "Write":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			else {
				p.safePrintf("<td><font color='red'><b>queued</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%"INT64"</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs) {
					p.safePrintf("<td>0/%"INT32"</td>", t->m_bytesToGo);
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>%s</td>",t->m_doWrite? "Write":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			p.safePrintf("</tr>"); 
		}
		p.safePrintf("</table><br><br>"); 

	}


	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						p.length() );

}
