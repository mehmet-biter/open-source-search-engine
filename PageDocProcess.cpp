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

#include <netinet/in.h>
#include <arpa/inet.h>
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h"
#include "GbUtil.h"
#include "DocDelete.h"
#include "DocRebuild.h"
#include "DocReindex.h"
#include "JobScheduler.h"

struct PageDocProcessState {
	PageDocProcessState(TcpSocket *s, HttpRequest *r, DocProcess *docProcess)
		: m_s(s)
		, m_r()
		, m_docProcess(docProcess) {
		m_r.copy(r);
	}

	TcpSocket *m_s;
	HttpRequest m_r;
	DocProcess *m_docProcess;
};

void waitPendingDocCountWrapper(void *state) {
	PageDocProcessState *pageDocProcessState = static_cast<PageDocProcessState*>(state);
	pageDocProcessState->m_docProcess->waitPendingDocCount(0);
}

void doneWaitPendingDocCountWrapper(void *state, job_exit_t exit_type) {
	PageDocProcessState *pageDocProcessState = static_cast<PageDocProcessState*>(state);

	if (exit_type != job_exit_normal) {
		g_httpServer.sendErrorReply(pageDocProcessState->m_s, ECANCELED, "job canceled");
		return;
	}

	g_httpServer.sendSuccessReply(pageDocProcessState->m_s, pageDocProcessState->m_r.getReplyFormat());
}

bool sendPageDocProcess(TcpSocket *s, HttpRequest *r) {
	int32_t keyLen = 0;
	const char *key = r->getString("key", &keyLen);
	std::string keyStr(key, keyLen);

	uint32_t firstIp = 0;
	int32_t firstIpLen = 0;
	const char *firstIpInput = r->getString("firstip", &firstIpLen);
	if (firstIpLen > 0) {
		std::string firstIpStr(firstIpInput, firstIpLen);

		in_addr addr;

		if (inet_pton(AF_INET, firstIpStr.c_str(), &addr) == 1) {
			firstIp = addr.s_addr;
		}
	}

	int32_t typeLen = 0;
	const char *type = r->getString("type", &typeLen);

	if (typeLen == 0) {
		return g_httpServer.sendErrorReply(s, EMISSINGINPUT, "missing parameter type");
	}

	DocProcess *docProcess = nullptr;

	switch (typeLen) {
		case 9:
			if (strncasecmp(type, "docdelete", 9) == 0) {
				// docdelete
				if (starts_with(keyStr.c_str(), "http")) {
					docProcess = &g_docDeleteUrl;
				} else {
					docProcess = &g_docDelete;
				}
			}
			break;
		case 10:
			if (strncasecmp(type, "docrebuild", 10) == 0) {
				// docrebuild
				if (starts_with(keyStr.c_str(), "http")) {
					docProcess = &g_docRebuildUrl;
				} else {
					docProcess = &g_docRebuild;
				}
			} else if (strncasecmp(type, "docreindex", 10) == 0) {
				// docreindex
				if (starts_with(keyStr.c_str(), "http")) {
					docProcess = &g_docReindexUrl;
				} else {
					docProcess = &g_docReindex;
				}
			}
			break;
		default:
			break;
	}

	if (docProcess) {
		docProcess->addKey(keyStr, firstIp);

		PageDocProcessState *state = new PageDocProcessState(s, r, docProcess);
		if (!g_jobScheduler.submit(waitPendingDocCountWrapper, doneWaitPendingDocCountWrapper, state, thread_type_page_process, 0)) {
			// unable to submit page
			return g_httpServer.sendErrorReply(s, EBADENGINEER, "unable to submit job");
		}

		return false;
	}

	return g_httpServer.sendErrorReply(s, EMISSINGINPUT, "invalid parameter type (docdelete, docrebuild, docreindex)");
}
