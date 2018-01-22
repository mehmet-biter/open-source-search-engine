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
#include "DocDelete.h"
#include "XmlDoc.h"

DocDelete g_docDelete("docdelete.txt", false);
DocDelete g_docDeleteUrl("docdeleteurl.txt", true);

DocDelete::DocDelete(const char *filename, bool isUrl)
	: DocProcess(filename, isUrl, false) {
}

void DocDelete::updateXmldoc(XmlDoc *xmlDoc) {
	xmlDoc->m_blockedDoc = false;
	xmlDoc->m_blockedDocValid = true;

	xmlDoc->m_deleteFromIndex = true;
}

void DocDelete::processDocItem(DocProcessDocItem *docItem) {
	XmlDoc *xmlDoc = docItem->m_xmlDoc;

	// set callback
	xmlDoc->m_masterLoop = processedDoc;
	xmlDoc->m_masterState = docItem;

	// prepare
	char **oldTitleRec = xmlDoc->getOldTitleRec();
	if (!oldTitleRec || oldTitleRec == (char**)-1) {
		return;
	}

	// oldTitleRec is mandatory for docdelete
	if (*oldTitleRec == nullptr) {
		xmlDoc->m_indexCode = ENOTFOUND;
		xmlDoc->m_indexCodeValid = true;

		xmlDoc->logIt();

		removePendingDoc(docItem);

		delete xmlDoc;
		delete docItem;

		return;
	}

	XmlDoc **oldXmlDoc = xmlDoc->getOldXmlDoc();
	if (!oldXmlDoc || oldXmlDoc == (XmlDoc **)-1) {
		// we must not be blocked/invalid at this point
		gbshutdownLogicError();
	}

	int32_t *firstIp = (*oldXmlDoc)->getFirstIp();
	if (!firstIp || firstIp == (int32_t *)-1) {
		// we must not be blocked/invalid at this point
		gbshutdownLogicError();
	}

	if (!xmlDoc->m_firstIpValid) {
		xmlDoc->m_firstIp = *firstIp;
		xmlDoc->m_firstIpValid = true;
		xmlDoc->m_sreq.m_firstIp = *firstIp;
	}

	// reset callback
	if (xmlDoc->m_masterLoop == processedDoc) {
		xmlDoc->m_masterLoop = nullptr;
		xmlDoc->m_masterState = nullptr;
	}

	// done
	if (xmlDoc->m_indexedDoc || xmlDoc->indexDoc()) {
		removePendingDoc(docItem);

		delete xmlDoc;
		delete docItem;
	}
}