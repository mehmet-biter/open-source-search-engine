#include "tokenizer_util.h"


UChar32 normal_to_superscript_codepoint(UChar32 c) {
	switch(c) {
		case '1': return 0x00B9;
		case '2': return 0x00B2;
		case '3': return 0x00B3;
		case '4': return 0x2074;
		case '5': return 0x2075;
		case '6': return 0x2076;
		case '7': return 0x2077;
		case '8': return 0x2078;
		case '9': return 0x2079;
		case 'a': return 0x00AA;
		case 'o': return 0x00BA;
		case 'i': return 0x2071;
		case 'n': return 0x207F;
//		case '+': return 0x207A;
//		case 0x2212: return 0x207B; //minus sign
//		case '=': return 0x207C;
//		case '(': return 0x207D;
//		case ')': return 0x207E;
		default: return 0;
	}
}

UChar32 normal_to_subscript_codepoint(UChar32 c) {
	switch(c) {
		case '0': return 0x2080;
		case '1': return 0x2081;
		case '2': return 0x2082;
		case '3': return 0x2083;
		case '4': return 0x2084;
		case '5': return 0x2085;
		case '6': return 0x2086;
		case '7': return 0x2087;
		case '8': return 0x2088;
		case '9': return 0x2089;
		case 'a': return 0x2090;
		case 'e': return 0x2091;
		case 'h': return 0x2095;
		case 'i': return 0x1D62;
		case 'j': return 0x2C7C;
		case 'k': return 0x2096;
		case 'l': return 0x2097;
		case 'm': return 0x2098;
		case 'n': return 0x2099;
		case 'o': return 0x2092;
		case 'p': return 0x209A;
		case 'r': return 0x1D63;
		case 's': return 0x209B;
		case 't': return 0x209C;
		case 'u': return 0x1D64;
		case 'v': return 0x1D65;
		case 'x': return 0x2093;
		default: return 0;
	}
}
