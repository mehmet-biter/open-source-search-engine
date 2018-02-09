File maps and how they are used.

The data comes from unicode.com and are in text format.
To speed up loading the filees are converted into binary format. The format depends on the data and how it is used.

==== Lookups
We are only interested in following lookups:

=== codepoint -> script (Scripts.txt)
All codepoints map to a script. The script can be latin/greek/cyrillic/... but also "common" and "inherited".

=== codepoint -> category (UnicodeData.txt)
A codepoint belongs to a general category such as uppercase_letter, number, punctuation, separator, ...
UnicodeData.txt uses 30+ categories.

=== ? -> lowercase
For hashing consistently we need to map from uppercase/titlecase to lowercase.

==== Optimal map types
Most of the codepoints we process are in the 0..128 range, so optimizing for that can be worth it. Some properties are in long ranges, so that may be worth optimizing for too. Some properties are sparse i.e. appleis only to a small subset of the available codepoints. So the optimal map type isn't clear-cut.

=== codepoint->script
All codepoints belong to a script (almost, there are some without in the private-use blocks that doesn't have them).
Optimal map type: full map, 1 byte per codepoint. Lookups will mostly hit the first 128 bytes. It May be worth adding hardcoded checks for the latin letters a-z and numbers, but tthose conditional branches may end up slower than just fetching from memory.

=== codepoint->properties
Codepoints can have a multiple of properties. UnicodeData.txt uses 34 distinct properties.
We ignore 2 of those properties and map the rest into a 32-bit bitmask. One 32-bit bitmask per codepoint.
Optimal map type: full map, 32 bit per entry.

Additional, testing if a codepoint can be in a word/identifier (a-z, 0-9, æ, greek letters, ...) is the most used lookup so we make a specialized map for that: full map, 1-byte boolean per entry.

=== upper/lowercase
We only need a mapping to lowercase. Lowercase only applies to a tiny fraction (~1300) of the codepoints, We currently only need the mapping to lowercase so we only dump that.
Optimal map type: sparse map, format:
	<codepoint>
	<lowercase codepoint count>
	[<codepoint>...]

=== Decomposition
A subset of the codepoints can be decomposed into (generally) <normal grapheme> + <diacritic...>
The subset is relatively small (~5700)
Optimal map type: sparse map, format:
	<codepoint>
	<decomposition codepoint count>
	[<decomposition codepoint>...]
