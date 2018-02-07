//
// Copyright (C) 2018 Privacore ApS - https://www.privacore.com
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
#include <arpa/inet.h>
#include "IpBlockList.h"
#include "Log.h"
#include "Conf.h"

IpBlockList g_ipBlockList;

static const char s_ip_filename[] = "ipblocklist.txt";

IpBlockList::IpBlockList()
	: BlockList(s_ip_filename) {
}

bool IpBlockList::isIpBlocked(uint32_t ip) {
	auto ipBlockList = getBlockList();

	for (auto const &ipBlock : *ipBlockList) {
		if (ipBlock == ip) {
			logTrace(g_conf.m_logTraceIpBlockList, "Ip block criteria %u matched ip '%u'", ipBlock, ip);
			return true;
		}
	}

	return false;
}

void IpBlockList::addToBlockList(blocklist_ptr_t<uint32_t> &blockList, const std::string &line) {
	in_addr addr;

	if (inet_pton(AF_INET, line.c_str(), &addr) != 1) {
		logTrace(g_conf.m_logTraceIpBlockList, "Ignoring invalid ip=%s", line.c_str());
		return;
	}

	blockList->emplace_back(addr.s_addr);
}