//Note: no external declaration. This file only contains the investigation results

/*
In the unicode data there are several codepoints that indicate that they are ligatures, eg. having
"ligature" in their name, or can be decomposed into two (or more) alphabetic codepoints. But it is
not that simple because users of those codepoints may consider them separate letters, in which case
decomposition will could change meaning.

The following candidate codepoints were found using strategic greps in UnicodeData.txt and visual
inspection of the glyphs.

Danish uses the ae as a separate letter. Cannot be decomposed in general.
It can be decomposed in English to "e" or "ae"
        00C6;LATIN CAPITAL LETTER AE;Lu;0;L;;;;;N;LATIN CAPITAL LETTER A E;;;00E6;
        00E6;LATIN SMALL LETTER AE;Ll;0;L;;;;;N;LATIN SMALL LETTER A E;;00C6;;00C6

Dutch seems to be the only users of the ij-ligature and they consider it a ligature. Can be decomposed.
        0132;LATIN CAPITAL LIGATURE IJ;Lu;0;L;<compat> 0049 004A;;;;N;LATIN CAPITAL LETTER I J;;;0133;
        0133;LATIN SMALL LIGATURE IJ;Ll;0;L;<compat> 0069 006A;;;;N;LATIN SMALL LETTER I J;;0132;;0132

English and French uses the oe-ligature in some words. It is unclear if they can be decomposed in
all languages, so we don't do that. It has to be language-dependent.
        0152;LATIN CAPITAL LIGATURE OE;Lu;0;L;;;;;N;LATIN CAPITAL LETTER O E;;;0153;
        0153;LATIN SMALL LIGATURE OE;Ll;0;L;;;;;N;LATIN SMALL LETTER O E;;0152;;0152

The d-z-with-caron is considered a separate letter in Croatian. Ditto for lj and nj. However, their typical
keyboards don't have a key for that so, they should be decomposed for indexing.
        01C5;LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON;Lt;0;L;<compat> 0044 017E;;;;N;LATIN LETTER CAPITAL D SMALL Z HACEK;;01C4;01C6;01C5
        01C6;LATIN SMALL LETTER DZ WITH CARON;Ll;0;L;<compat> 0064 017E;;;;N;LATIN SMALL LETTER D Z HACEK;;01C4;;01C5
        01C7;LATIN CAPITAL LETTER LJ;Lu;0;L;<compat> 004C 004A;;;;N;LATIN CAPITAL LETTER L J;;;01C9;01C8
        01C8;LATIN CAPITAL LETTER L WITH SMALL LETTER J;Lt;0;L;<compat> 004C 006A;;;;N;LATIN LETTER CAPITAL L SMALL J;;01C7;01C9;01C8
        01C9;LATIN SMALL LETTER LJ;Ll;0;L;<compat> 006C 006A;;;;N;LATIN SMALL LETTER L J;;01C7;;01C8
        01CA;LATIN CAPITAL LETTER NJ;Lu;0;L;<compat> 004E 004A;;;;N;LATIN CAPITAL LETTER N J;;;01CC;01CB
        01CB;LATIN CAPITAL LETTER N WITH SMALL LETTER J;Lt;0;L;<compat> 004E 006A;;;;N;LATIN LETTER CAPITAL N SMALL J;;01CA;01CC;01CB
        01CC;LATIN SMALL LETTER NJ;Ll;0;L;<compat> 006E 006A;;;;N;LATIN SMALL LETTER N J;;01CA;;01CB

According to wikipedia Hungarian uses dz-ligature and considers it a separate letter. But my Hungarian contact
didn't know that there was a codepoint with that ligature and he said no keyboard has ever had it.
        01F1;LATIN CAPITAL LETTER DZ;Lu;0;L;<compat> 0044 005A;;;;N;;;;01F3;01F2
        01F2;LATIN CAPITAL LETTER D WITH SMALL LETTER Z;Lt;0;L;<compat> 0044 007A;;;;N;;;01F1;01F3;01F2
        01F3;LATIN SMALL LETTER DZ;Ll;0;L;<compat> 0064 007A;;;;N;;;01F1;;01F2

Seems to be separate letters:
        04A4;CYRILLIC CAPITAL LIGATURE EN GHE;Lu;0;L;;;;;N;CYRILLIC CAPITAL LETTER EN GE;;;04A5;
        04A5;CYRILLIC SMALL LIGATURE EN GHE;Ll;0;L;;;;;N;CYRILLIC SMALL LETTER EN GE;;04A4;;04A4
        04B4;CYRILLIC CAPITAL LIGATURE TE TSE;Lu;0;L;;;;;N;CYRILLIC CAPITAL LETTER TE TSE;;;04B5;
        04B5;CYRILLIC SMALL LIGATURE TE TSE;Ll;0;L;;;;;N;CYRILLIC SMALL LETTER TE TSE;;04B4;;04B4
        04D4;CYRILLIC CAPITAL LIGATURE A IE;Lu;0;L;;;;;N;;;;04D5;
        04D5;CYRILLIC SMALL LIGATURE A IE;Ll;0;L;;;;;N;;;04D4;;04D4

Seems to be a stylistic ligature
        0587;ARMENIAN SMALL LIGATURE ECH YIWN;Ll;0;L;<compat> 0565 0582;;;;N;;;;;

Seems to be separate letters:
        05F0;HEBREW LIGATURE YIDDISH DOUBLE VAV;Lo;0;R;;;;;N;HEBREW LETTER DOUBLE VAV;;;;
        05F1;HEBREW LIGATURE YIDDISH VAV YOD;Lo;0;R;;;;;N;HEBREW LETTER VAV YOD;;;;
        05F2;HEBREW LIGATURE YIDDISH DOUBLE YOD;Lo;0;R;;;;;N;HEBREW LETTER DOUBLE YOD;;;;

Stylistic ligatures:
        FB00;LATIN SMALL LIGATURE FF;Ll;0;L;<compat> 0066 0066;;;;N;;;;;
        FB01;LATIN SMALL LIGATURE FI;Ll;0;L;<compat> 0066 0069;;;;N;;;;;
        FB02;LATIN SMALL LIGATURE FL;Ll;0;L;<compat> 0066 006C;;;;N;;;;;
        FB03;LATIN SMALL LIGATURE FFI;Ll;0;L;<compat> 0066 0066 0069;;;;N;;;;;
        FB04;LATIN SMALL LIGATURE FFL;Ll;0;L;<compat> 0066 0066 006C;;;;N;;;;;
        FB05;LATIN SMALL LIGATURE LONG S T;Ll;0;L;<compat> 017F 0074;;;;N;;;;;
        FB06;LATIN SMALL LIGATURE ST;Ll;0;L;<compat> 0073 0074;;;;N;;;;;

Plain digraphs with no decompositition
        0238;LATIN SMALL LETTER DB DIGRAPH;Ll;0;L;;;;;N;;;;;
        0239;LATIN SMALL LETTER QP DIGRAPH;Ll;0;L;;;;;N;;;;;
        02A3;LATIN SMALL LETTER DZ DIGRAPH;Ll;0;L;;;;;N;LATIN SMALL LETTER D Z;;;;
        02A4;LATIN SMALL LETTER DEZH DIGRAPH;Ll;0;L;;;;;N;LATIN SMALL LETTER D YOGH;;;;
        02A5;LATIN SMALL LETTER DZ DIGRAPH WITH CURL;Ll;0;L;;;;;N;LATIN SMALL LETTER D Z CURL;;;;
        02A6;LATIN SMALL LETTER TS DIGRAPH;Ll;0;L;;;;;N;LATIN SMALL LETTER T S;;;;
        02A7;LATIN SMALL LETTER TESH DIGRAPH;Ll;0;L;;;;;N;LATIN SMALL LETTER T ESH;;;;
        02A8;LATIN SMALL LETTER TC DIGRAPH WITH CURL;Ll;0;L;;;;;N;LATIN SMALL LETTER T C CURL;;;;
        02A9;LATIN SMALL LETTER FENG DIGRAPH;Ll;0;L;;;;;N;;;;;
        02AA;LATIN SMALL LETTER LS DIGRAPH;Ll;0;L;;;;;N;;;;;
        02AB;LATIN SMALL LETTER LZ DIGRAPH;Ll;0;L;;;;;N;;;;;
        0478;CYRILLIC CAPITAL LETTER UK;Lu;0;L;;;;;N;CYRILLIC CAPITAL LETTER UK DIGRAPH;;;0479;
        0479;CYRILLIC SMALL LETTER UK;Ll;0;L;;;;;N;CYRILLIC SMALL LETTER UK DIGRAPH;;0478;;0478

Uhm....
        0276;LATIN LETTER SMALL CAPITAL OE;Ll;0;L;;;;;N;LATIN LETTER SMALL CAPITAL O E;;;;
        309F;HIRAGANA DIGRAPH YORI;Lo;0;L;<vertical> 3088 308A;;;;N;;;;;
        30FF;KATAKANA DIGRAPH KOTO;Lo;0;L;<vertical> 30B3 30C8;;;;N;;;;;


note: the digraphs in IPA extension 02A3..02AB are not decomposed

Oh dear.. The German sharp s is just trouble.
It normally only exist in lowercase form, and decomposes to "ss" or "sz" in uppercase. And
unicode has an uppercase variant with is starting to see some use. So for indexing purposes
we cannot really decompose it to ss or sz. And we will rarely encounter the uppercase
variant. So let's leave it be until we are sure.
        00DF;LATIN SMALL LETTER SHARP S;Ll;0;L;;;;;N;;;;;
        1E9E;LATIN CAPITAL LETTER SHARP S;Lu;0;L;;;;;N;;;;00DF;
*/
