#include "gb-include.h"

#include "Entities.h"
#include "Unicode.h"
#include "HashTableX.h"


static HashTableX s_table;
static bool       s_isInitialized = false;
struct Entity {
	const char     *entity;        //entity name with leading ampersand but without trailing semicolon, like "&nbsp"
	int             codepoints;    //number of unicode codepoitns this entity translates to
	int             codepoint[2];  //unicode codepoints
	size_t          utf8Len;       //length of utf8
	char            utf8[2*4];     //2 codepoints of 4 bytes each
};

#include "entities.inc"


void resetEntities ( ) {
	s_table.reset();
}

static bool initEntityTable(){
	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( 8,4,4096,NULL,0,false,0,"enttbl" ) )
			return log("build: Could not init table of "
					   "HTML entities.");
		// now add in all the html entities
		const int32_t n = (int32_t)sizeof(s_entities) / (int32_t)sizeof(Entity);
		for ( int32_t i = 0 ; i < n ; i++ ) {
			int64_t h = hash64b ( s_entities[i].entity );

			// convert the unicode codepoints to an utf8 string
			char *buf = (char *)s_entities[i].utf8;
			for(int j=0; j<s_entities[i].codepoints; j++) {
				UChar32 codepoint = s_entities[i].codepoint[j];
				int32_t len = utf8Encode(codepoint,buf);
				if ( len == 0 ) { char *xx=NULL;*xx=0; }
				
				// make modification to make parsing easier
				if ( codepoint == 160 ) {  // nbsp
					buf[0] = ' ';
					len = 1;
				}
				buf += len;
				
			}
			s_entities[i].utf8Len = (size_t)(buf-s_entities[i].utf8);
			// must not exist!
			if ( s_table.isInTable(&h) ) { char*xx=NULL;*xx=0;}
			// store the entity index in the hash table as score
			if ( ! s_table.addTerm ( &h, i+1 ) ) return false;
		}
		s_isInitialized = true;
	} 
	return true;
}


// . is "s" an HTML entity? (ascii representative of an iso char)
// . return the 32-bit unicode char it represents
// . returns 0 if none
// . JAB: const-ness for optimizer...
static uint32_t getTextEntity ( const char *s , int32_t len ) {
	if ( !initEntityTable()) return 0;
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// compute the hash of the entity including &, but not ;
	int64_t h = hash64 ( s , len );
	// get the entity index from table (stored in the score field)
	int32_t i = (int32_t) s_table.getScore ( &h );
	// return 0 if no match
	if ( i == 0 ) return 0;
	// point to the utf8 char. these is 1 or 2 bytes it seems
	char *p = (char *)s_entities[i-1].utf8;
	// encode into unicode
	uint32_t c = utf8Decode ( p );
	// return that
	return c;
}

// . get a decimal encoded entity
// . s/len is the whol thing
// . JAB: const-ness for optimizer...
static uint32_t getDecimalEntity ( const char *s , int32_t len ) {
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// . &#1 is smallest it can be
	// . &#1114111 is biggest
	if ( len < 3  ||  len > 9 ) return 0;
	// . must start with &#[0-9]
	if ( s[0] !='&'  ||  s[1] != '#' || ! is_digit(s[2]) ) return 0;
	// use space as default
	uint32_t v ;
	if ( len == 3 ) v = (s[2]-48); 
	else if ( len == 4 ) v = (s[2]-48)*10    +
				     (s[3]-48);
	else if ( len == 5 ) v = (s[2]-48)*100   +
				     (s[3]-48)*10  +
				     (s[4]-48);
	else if ( len == 6 ) v = (s[2]-48)*1000  +
				     (s[3]-48)*100 +
				     (s[4]-48)*10 +
				     s[5]-48;
	else if ( len == 7 ) v = (s[2]-48)*10000 +
				     (s[3]-48)*1000+
				     (s[4]-48)*100+
				     (s[5]-48)*10+
				     s[5]-48;
	else if ( len == 8 ) v = (s[2]-48)*100000 +
				     (s[3]-48)*10000 +
				     (s[4]-48)*1000+
				     (s[5]-48)*100+
				     (s[6]-48)*10+
				     s[7]-48;
	else if ( len == 9 ) v = (s[2]-48)*1000000 +
				     (s[3]-48)*100000 +
				     (s[4]-48)*10000 +
				     (s[5]-48)*1000 +
				     (s[6]-48)*100 +
				     (s[7]-48)*10 +
				     s[7]-48;
	else return (uint32_t)' ';

	//printf("Translated entity (dec)");
	//for (int i=0;i<len;i++)putchar(s[i]);
	//printf(" to [U+%" PRId32"]\n", v);

	if (v < 32 || v>0x10ffff) return (uint32_t)' ';

	return v;
}		


// . get a hexadecimal encoded entity
// . JAB: const-ness for optimizer...
// . returns a UChar32
static uint32_t getHexadecimalEntity ( const char *s , int32_t len ) {
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// . &#x1  is smallest it can be
	// . &#x10FFFF is biggest
	if ( len < 4  ||  len > 9 ) return (char)0;
	// . must start with &#x[0-f]
	if ( s[0] !='&'  ||  s[1] != '#' ||  s[2] !='x'  ) return (char)0;
	if ( ! is_hex ( s[3] ) ) return (char)0;
	// use space as default
	uint32_t v;
	if      ( len == 4 ) v = htob(s[3]);
	else if ( len == 5 ) v = (htob(s[3]) << 4) + 
			htob(s[4]);
	else if ( len == 6 ) v = (htob(s[3]) << 8) + 
		(htob(s[4]) << 4) + 
		htob(s[5]);
	else if ( len == 7 ) v = (htob(s[3]) << 12) + 
		(htob(s[4]) << 8) + 
		(htob(s[5]) << 4) +
				htob(s[6]);
	else if ( len == 8 ) v = (htob(s[3]) << 16) + 
		(htob(s[4]) << 12) + 
		(htob(s[5]) << 8) +
		(htob(s[6]) << 4) +
		htob(s[7]);
	else if ( len == 9 ) v = (htob(s[3]) << 20) + 
		(htob(s[4]) << 16) + 
		(htob(s[5]) << 12) +
		(htob(s[6]) << 8) +
		(htob(s[7]) << 4) +
		htob(s[8]);
	else 
		return (uint32_t)' ';
	// return the char
	//printf("Translated entity (dec)");
	//for (int i=0;i<len;i++)putchar(s[i]);
	//printf(" to [U+%04lX]\n", v);
	if (v < 32 || v>0x10ffff) return (uint32_t)' ';
	return (uint32_t) v;
}		


// . s[maxLen] should be the NULL
// . returns full length of entity @ "s" if there is a valid one, 0 otherwise
// . sets *c to the iso character the entity represents (if there is one)
// JAB: const-ness for optimizer...
int32_t getEntity_a ( const char *s , int32_t maxLen , uint32_t *c ) {
	//TODO: handle multi-codepoint entitites
	// ensure there's an & as first char
	if ( s[0] != '&' ) {
		return 0;
	}

	// compute maximum length of entity, if it's indeed an entity
	int32_t len = 1;
	if ( s[len] == '#' ) {
		len++;
	}

	// cut it off after <32> chars to save time and also to avoid parsing
	// obscenely long incorrect entitites (eg an ampersand followed by 2MB of letters)
	while ( len < maxLen && len < max_entity_name_len && is_alnum_a( s[len] ) ) {
		len++;
	}

	// character entity reference must end with a semicolon.
	// some browsers have lenient parsing, but we don't accept invalid
	// references.
	if ( len == maxLen || s[len] != ';' ) {
		//not a valid character entity reference
		return 0;
	}
	len++;

	// we don't have entities longer than what w3c specified
	if ( len > max_entity_name_len+1 ) {
		return 0;
	}

	// all entites are 3 or more chars (&gt)
	if ( len < 3 ) {
		return 0;
	}

	// . if it's a numeric entity like &#123 use this routine
	// . pass in the whole she-bang: "&#12...;" or "&acute...;
	if ( s[1] == '#' ) {
		if ( s[2] == 'x' ) {
			*c = getHexadecimalEntity( s, len );
		} else {
			*c = getDecimalEntity( s, len );
		}
	} else {
		// otherwise, it's text
		*c = getTextEntity( s, len );
	}

	// return 0 if not an entity, length of entity if it is an entity
	if ( *c ) {
		return len;
	} else {
		return 0;
	}
}
