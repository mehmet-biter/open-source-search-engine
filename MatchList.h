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
#ifndef FX_MATCHLIST_H
#define FX_MATCHLIST_H


#include <memory>
#include <vector>
#include <string>
#include <atomic>

template <typename T> using matchlist_t = std::vector<T>;
template <typename T> using matchlist_ptr_t = std::shared_ptr<std::vector<T>>;
template <typename T> using matchlistconst_ptr_t = std::shared_ptr<const std::vector<T>>;

template<class T> class MatchList {
public:
	explicit MatchList(const char *filename);
	virtual ~MatchList() = default;

	virtual bool init();

	static void reload(int /*fd*/, void *state);
	static void reload(void *state);

protected:
	bool load();

	virtual void addToMatchList(matchlist_ptr_t<T> &matchList, const std::string &line);
	matchlistconst_ptr_t<T> getMatchList();

	const char *m_filename;

private:
	void swapMatchList(matchlistconst_ptr_t<T> matchList);

	std::atomic_bool m_loading;
	matchlistconst_ptr_t<T> m_matchList;

	time_t m_lastModifiedTime;
};

#endif //FX_MATCHLIST_H
