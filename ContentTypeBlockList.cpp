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
#include "ContentTypeBlockList.h"
#include "ScopedLock.h"
#include "Log.h"
#include "Conf.h"
#include <algorithm>
#include <fstream>

ContentTypeBlockList g_contentTypeBlockList;

static const char s_contenttype_filename[] = "contenttypeblocklist.txt";
static const char s_contenttype_allowed_filename[] = "contenttypeallowed.txt";

ContentTypeBlockList::ContentTypeBlockList()
	: BlockList(s_contenttype_filename)
	, m_contenttype_allowed()
	, m_contenttype_allowed_mtx(PTHREAD_MUTEX_INITIALIZER) {
}

bool ContentTypeBlockList::init() {
	std::ifstream file(s_contenttype_allowed_filename);
	std::string line;

	ScopedLock sl(m_contenttype_allowed_mtx);
	while (std::getline(file, line)) {
		m_contenttype_allowed.push_back(line);
	}

	return BlockList::init();
}

void ContentTypeBlockList::addContentTypeAllowed(const char *contentType, size_t contentTypeLen) {
	std::string contentTypeStr(contentType, contentTypeLen);
	std::transform(contentTypeStr.begin(), contentTypeStr.end(), contentTypeStr.begin(), ::tolower);

	ScopedLock sl(m_contenttype_allowed_mtx);
	if (std::find(m_contenttype_allowed.begin(), m_contenttype_allowed.end(), contentTypeStr) != m_contenttype_allowed.end()) {
		return;
	}

	m_contenttype_allowed.push_back(contentTypeStr);
	std::ofstream file(s_contenttype_allowed_filename, (std::ios::out | std::ios::app));
	file << contentTypeStr << std::endl;
}

bool ContentTypeBlockList::isContentTypeBlocked(const char *contentType, size_t contentTypeLen) {
	if (contentTypeLen == 0) {
		return false;
	}

	auto contentTypeBlockList = getBlockList();

	for (auto const &contentTypeBlock : *contentTypeBlockList) {
		if (contentTypeBlock.back() == '*') {
			// prefix
			if (contentTypeLen >= contentTypeBlock.size() - 1 && strncasecmp(contentTypeBlock.c_str(), contentType, contentTypeBlock.size() - 1) == 0) {
				logTrace(g_conf.m_logTraceContentTypeBlockList, "Content type block criteria %s matched contenttype '%.*s'", contentTypeBlock.c_str(), static_cast<int>(contentTypeLen), contentType);
				return true;
			}
		} else {
			if (contentTypeLen == contentTypeBlock.size() && strncasecmp(contentTypeBlock.c_str(), contentType, contentTypeLen) == 0) {
				logTrace(g_conf.m_logTraceContentTypeBlockList, "Content type block criteria %s matched contenttype '%.*s'", contentTypeBlock.c_str(), static_cast<int>(contentTypeLen), contentType);
				return true;
			}
		}
	}

	addContentTypeAllowed(contentType, contentTypeLen);
	return false;
}
