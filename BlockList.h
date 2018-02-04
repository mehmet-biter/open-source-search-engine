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
#ifndef FX_BLOCKLIST_H
#define FX_BLOCKLIST_H


#include <memory>
#include <vector>
#include <string>
#include <atomic>

template <typename T> using blocklist_t = std::vector<T>;
template <typename T> using blocklist_ptr_t = std::shared_ptr<std::vector<T>>;
template <typename T> using blocklistconst_ptr_t = std::shared_ptr<const std::vector<T>>;
//typedef std::vector<std::string> blocklist_t;
//typedef std::shared_ptr<blocklist_t> blocklist_ptr_t;
//typedef std::shared_ptr<const blocklist_t> blocklistconst_ptr_t;

template<class T> class BlockList {
//using blocklist_t = std::vector<T>,  using blocklist_ptr_t = std::shared_ptr<blocklist_t>, using blocklistconst_ptr_t = std::shared_ptr<const blocklist_t>{
public:
	BlockList(const char *filename);

	bool init();

	static void reload(int /*fd*/, void *state);
	static void reload(void *state);

protected:
	bool load();

	const char *m_filename;

	blocklistconst_ptr_t<T> getBlockList();

private:
	void swapBlockList(blocklistconst_ptr_t<T> blockList);

	std::atomic_bool m_loading;
	blocklistconst_ptr_t<T> m_blockList;

	time_t m_lastModifiedTime;
};

#endif //FX_BLOCKLIST_H
