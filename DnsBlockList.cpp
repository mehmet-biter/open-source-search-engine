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
#include "DnsBlockList.h"
#include "Log.h"
#include "Conf.h"

DnsBlockList g_dnsBlockList;

static const char s_dns_filename[] = "dnsblocklist.txt";

DnsBlockList::DnsBlockList()
	: MatchList(s_dns_filename) {
}

bool DnsBlockList::isDnsBlocked(const char *dns) {
	auto dnsBlockList = getMatchList();

	for (auto const &dnsBlock : *dnsBlockList) {
		if (dnsBlock.front() == '*') {
			// wildcard
			size_t dnsLen = strlen(dns);
			if (dnsLen >= dnsBlock.length() - 1 && strcasecmp(dnsBlock.c_str() + 1, dns + (dnsLen - (dnsBlock.length() - 1))) == 0) {
				logTrace(g_conf.m_logTraceDnsBlockList, "Dns block criteria %s matched dns '%s'", dnsBlock.c_str(), dns);
				return true;
			}
		} else {
			if (strcasecmp(dnsBlock.c_str(), dns) == 0) {
				logTrace(g_conf.m_logTraceDnsBlockList, "Dns block criteria %s matched dns '%s'", dnsBlock.c_str(), dns);
				return true;
			}
		}
	}

	return false;
}
