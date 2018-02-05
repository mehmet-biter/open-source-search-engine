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
#ifndef FX_IPBLOCKLIST_H
#define FX_IPBLOCKLIST_H

#include "BlockList.h"

class IpBlockList : public BlockList<uint32_t> {
public:
	IpBlockList();
	bool isIpBlocked(uint32_t ip);

protected:
	void addToBlockList(blocklist_ptr_t<uint32_t> &blockList, const std::string &line);

};

extern IpBlockList g_ipBlockList;

#endif //FX_IPBLOCKLIST_H
