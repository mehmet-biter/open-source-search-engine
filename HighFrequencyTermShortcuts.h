#ifndef HIGHFREQUENCYTERMSHORTCUTS_H_
#define HIGHFREQUENCYTERMSHORTCUTS_H_

#include <inttypes.h>
#include <map>
#include <stddef.h>

//A set of PosDB shortcuts for high-frequency terms, aka stop words
class HighFrequencyTermShortcuts {
	HighFrequencyTermShortcuts(const HighFrequencyTermShortcuts&);
	HighFrequencyTermShortcuts& operator=(const HighFrequencyTermShortcuts&);

	struct TermEntry {
		const void *p;
		size_t bytes;
	};
	std::map<uint64_t,TermEntry> entries;
	void *buffer;
public:
	HighFrequencyTermShortcuts()
	  : entries()
	  { }
	~HighFrequencyTermShortcuts()
	  { }
	
	bool load();
	void unload();
	
	bool empty() const { return entries.empty(); }
	
	bool query_term_shortcut(uint64_t term_id, const void **posdb_entries, size_t *bytes);
};

extern HighFrequencyTermShortcuts g_hfts;

#endif //HIGHFREQUENCYTERMSHORTCUTS_H_
