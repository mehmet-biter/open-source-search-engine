//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#ifndef FX_DOCPROCESS_H
#define FX_DOCPROCESS_H

#include "GbMutex.h"
#include "GbThreadQueue.h"
#include <vector>
#include <atomic>
#include <string>

class XmlDoc;

class DocProcess {
public:
	DocProcess(const char *filename, bool isUrl, void (*updateXmldocFunc)(XmlDoc *xmlDoc));

	bool init();
	void finalize();

	static void reload(int /*fd*/, void */*state*/);
	static void processFile(void *item);
	static void processDoc(void *item);
	static void processedDoc(void *state);

private:
	void waitPendingDocCount(unsigned maxCount);
	void addPendingDoc(struct DocProcessDocItem *docItem);
	void removePendingDoc(struct DocProcessDocItem *docItem);

	const char *m_filename;
	std::string m_tmpFilename;
	std::string m_lastPosFilename;

	time_t m_lastModifiedTime;

	bool m_isUrl;

	void (*m_updateXmldocFunc)(XmlDoc *xmlDoc);
	std::vector<struct DocProcessDocItem*> m_pendingDocItems;
	GbMutex m_pendingDocItemsMtx;
	pthread_cond_t m_pendingDocItemsCond;

	std::atomic<bool> m_stop;
};


#endif //FX_DOCPROCESS_H
