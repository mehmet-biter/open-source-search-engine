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
#include "DocReindex.h"
#include "XmlDoc.h"
#include "Msg0.h"
#include "RdbList.h"
#include "Conf.h"
#include "TitleRecVersion.h"

DocReindex g_docReindex("docreindex.txt", false);
DocReindex g_docReindexUrl("docreindexurl.txt", true);

struct DocReindexDocItem : public DocProcessDocItem {
	DocReindexDocItem(DocProcess *docProcess, const std::string &key, uint32_t firstIp, int64_t lastPos)
		: DocProcessDocItem(docProcess, key, firstIp, lastPos)
		, m_msg0()
		, m_spiderdbList()
		, m_spiderdbListRequested(false)
		, m_spiderdbListProcessed(false) {
	}

	Msg0 m_msg0;
	RdbList m_spiderdbList;
	bool m_spiderdbListRequested;
	bool m_spiderdbListProcessed;
};

DocReindex::DocReindex(const char *filename, bool isUrl)
	: DocProcess(filename, isUrl, !isUrl) {
}

DocProcessDocItem* DocReindex::createDocItem(DocProcess *docProcess, const std::string &key, uint32_t firstIp, int64_t lastPos) {
	return new DocReindexDocItem(docProcess, key, firstIp, lastPos);
}

void DocReindex::updateXmldoc(XmlDoc *xmlDoc) {
	xmlDoc->m_indexCodeValid = false;

#ifndef PRIVACORE_SAFE_VERSION
	xmlDoc->m_version = g_conf.m_titleRecVersion;
#else
	xmlDoc->m_version = TITLEREC_CURRENT_VERSION;
#endif

	xmlDoc->m_versionValid = true;
}

void DocReindex::processDocItem(DocProcessDocItem *docItem) {
	DocReindexDocItem *reindexDocItem = dynamic_cast<DocReindexDocItem*>(docItem);
	if (reindexDocItem == nullptr) {
		gbshutdownLogicError();
	}

	XmlDoc *xmlDoc = reindexDocItem->m_xmlDoc;

	// set callback
	xmlDoc->m_masterLoop = processedDoc;
	xmlDoc->m_masterState = reindexDocItem;

	// prepare
	char **oldTitleRec = xmlDoc->getOldTitleRec();
	if (!oldTitleRec || oldTitleRec == (char**)-1) {
		return;
	}

	// oldTitleRec is mandatory for docid based docProcess
	if (!m_isUrl && *oldTitleRec == nullptr) {
		xmlDoc->m_indexCode = ENOTFOUND;
		xmlDoc->m_indexCodeValid = true;

		xmlDoc->logIt();

		removePendingDoc(reindexDocItem);

		delete xmlDoc;
		delete reindexDocItem;

		return;
	}

	int32_t *firstIp = xmlDoc->getFirstIp();
	if (!firstIp || firstIp == (int32_t *)-1) {
		// blocked
		return;
	}

	// reset callback
	if (xmlDoc->m_masterLoop == processedDoc) {
		xmlDoc->m_masterLoop = nullptr;
		xmlDoc->m_masterState = nullptr;
	}

	// set spider request
	if (!reindexDocItem->m_spiderdbListRequested) {
		int64_t urlHash48 = xmlDoc->getFirstUrlHash48();
		key128_t startKey = Spiderdb::makeKey(*firstIp, urlHash48, true, 0, false);
		key128_t endKey = Spiderdb::makeKey(*firstIp, urlHash48, true, MAX_DOCID, false);

		reindexDocItem->m_spiderdbListRequested = true;

		if (!reindexDocItem->m_msg0.getList(-1, RDB_SPIDERDB_DEPRECATED, xmlDoc->m_collnum, &reindexDocItem->m_spiderdbList, (const char *)&startKey,
		                             (const char *)&endKey,
		                             1000000, reindexDocItem, processedDoc, 0, true, true, -1, 0, -1, 10000, false, false, -1)) {
			// blocked
			return;
		}
	}

	if (!reindexDocItem->m_spiderdbListProcessed) {
		if (reindexDocItem->m_spiderdbList.isEmpty()) {
			xmlDoc->getRebuiltSpiderRequest(&xmlDoc->m_sreq);
			xmlDoc->m_addSpiderRequest = true;
		} else {
			SpiderRequest *sreq = reinterpret_cast<SpiderRequest *>(reindexDocItem->m_spiderdbList.getCurrentRec());
			memcpy(&xmlDoc->m_sreq, sreq, sreq->m_dataSize + sizeof(key128_t) + 4);
		}

		xmlDoc->m_sreqValid = true;
		reindexDocItem->m_spiderdbListProcessed = true;
	}

	// done
	if (xmlDoc->m_indexedDoc || xmlDoc->indexDoc()) {
		removePendingDoc(reindexDocItem);

		delete xmlDoc;
		delete reindexDocItem;
	}
}
