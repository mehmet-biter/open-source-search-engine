#ifndef GB_HIGHFREQUENCYTERMSHORTCUTS_H
#define GB_HIGHFREQUENCYTERMSHORTCUTS_H

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
		char start_key[18];
		char end_key[18];
	};
	std::map<uint64_t,TermEntry> entries;
	void *buffer;
public:
	HighFrequencyTermShortcuts() : entries() { 
		buffer=NULL;
	}

	~HighFrequencyTermShortcuts()
	  { }
	
	bool load();
	void unload();
	
	bool empty() const { return entries.empty(); }
	
	bool query_term_shortcut(uint64_t term_id,
	                         const void **posdb_entries, size_t *bytes,
	                         void *start_key, void *end_key);

	bool is_registered_term(uint64_t term_id);
};

extern HighFrequencyTermShortcuts g_hfts;

#endif // GB_HIGHFREQUENCYTERMSHORTCUTS_H
