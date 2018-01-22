//This file is developed by Privacore ApS and covered by the GNU Affero General Public License. See LICENSE for details.
#include "FxExplicitKeywords.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "Log.h"
#include <map>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

static const char filename[] = "explicit_keywords.txt";

static GbMutex mtx;
static std::map<std::string,std::string> m;
static const time_t check_interval = 10;
static time_t next_check_time = 0;
static time_t last_mtime = 0;


static void reload_if_needed(bool detailed_log) {
	time_t now=time(0);
	if(now<next_check_time)
		return;
	next_check_time = now + check_interval;
	
	FILE *fp = fopen(filename,"r");
	if(!fp) {
		if(errno!=ENOENT) //normal
			log(LOG_ERROR,"fopen(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		else if(detailed_log)
			log(LOG_DEBUG,"%s doesn't exist", filename);
		return;
	}
	
	struct stat st;
	if(fstat(fileno(fp),&st)!=0) {
		log(LOG_ERROR, "fstat(%s) failed with errno=%d", filename, errno);
		fclose(fp);
		return;
	}
	if(st.st_mtime==last_mtime) {
		//hasn't changed
		fclose(fp);
		if(detailed_log)
			log(LOG_DEBUG,"%s hasn't changed since last time", filename);
		return;
	}
	if(last_mtime==0)
		log(LOG_INFO, "Loading %s", filename);
	else
		log(LOG_INFO, "%s has changed. Reloading it", filename);
	last_mtime = st.st_mtime;
	
	//The file has changed
	int entry_count = 0;
	std::map<std::string,std::string> new_m;
	char line[4096];
	while(fgets(line,sizeof(line),fp)) {
		if(line[0]=='#')
			continue;
		char *p;
		p = strchr(line,'\n');
		if(p) *p='\0';
		p = strchr(line,'\r'); //why are you using a dos editor?
		if(p) *p='\0';

		p = strchr(line,'\t');
		if(!p)
			continue;
		*p = '\0';
		const char *url = p+1;
		
		new_m[url] = line;
		entry_count++;
	}
	fclose(fp);
	
	ScopedLock sl(mtx);
	m.swap(new_m);
	sl.unlock();
	log(LOG_INFO, "%s reloaded. Entries: %d", filename, entry_count);
}


bool ExplicitKeywords::initialize() {
	reload_if_needed(true);
	return true;
}


void ExplicitKeywords::finalize() {
	m.clear();
}


std::string ExplicitKeywords::lookupExplicitKeywords(const std::string &url) {
	reload_if_needed(false);
	ScopedLock sl(mtx);
	auto iter = m.find(url);
	if(iter!=m.end())
		return iter->second;
	else
		return "";
}
