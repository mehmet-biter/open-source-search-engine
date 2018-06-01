#ifndef LIGATURE_DECOMPOSITION_H_
#define LIGATURE_DECOMPOSITION_H_

//Decompose stylistic ligatures
//The input codepoint is decomposed into 2 or more codepoints. Case and combining marks are retained.
//Hardcoded, not table-driven. For reasons see corresponding .cpp file
static inline unsigned decompose_stylistic_ligature(UChar32 c, UChar32 dc[4]) {
        switch(c) {
                case 0x0132: //LATIN CAPITAL LIGATURE IJ
                        dc[0] = 'I';
                        dc[1] = 'J';
                        return 2;
                case 0x0133: //LATIN SMALL LIGATURE IJ
                        dc[0] = 'i';
                        dc[1] = 'j';
                        return 2;
                case 0x01C4: //LATIN CAPITAL LETTER DZ WITH CARON
                        dc[0] = 'D';
                        dc[1] = 0x017D;
                        return 2;
                case 0x01C5: //LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON
                        dc[0] = 'D';
                        dc[1] = 0x017E;
                        return 2;
                case 0x01C6: //LATIN SMALL LETTER DZ WITH CARON
                        dc[0] = 'd';
                        dc[1] = 0x017E;
                        return 2;
                case 0x01C7: //LATIN CAPITAL LETTER LJ
                        dc[0] = 'L';
                        dc[1] = 'J';
                        return 2;
                case 0x01C8: //LATIN CAPITAL LETTER L WITH SMALL LETTER J
                        dc[0] = 'L';
                        dc[1] = 'j';
                        return 2;
                case 0x01C9: //LATIN SMALL LETTER LJ
                        dc[0] = 'l';
                        dc[1] = 'j';
                        return 2;
                case 0x01CA: //LATIN CAPITAL LETTER NJ
                        dc[0] = 'N';
                        dc[1] = 'J';
                        return 2;
                case 0x01CB: //LATIN CAPITAL LETTER N WITH SMALL LETTER J
                        dc[0] = 'N';
                        dc[1] = 'j';
                        return 2;
                case 0x01CC: //LATIN SMALL LETTER NJ
                        dc[0] = 'n';
                        dc[1] = 'j';
                        return 2;
                case 0x01F1: //LATIN CAPITAL LETTER DZ
                        dc[0] = 'D';
                        dc[1] = 'Z';
                        return 2;
                case 0x01F2: //LATIN CAPITAL LETTER D WITH SMALL LETTER Z
                        dc[0] = 'D';
                        dc[1] = 'z';
                        return 2;
                case 0x01F3: //LATIN SMALL LETTER DZ
                        dc[0] = 'd';
                        dc[1] = 'z';
                        return 2;
                case 0x0587: //ARMENIAN SMALL LIGATURE ECH YIWN
                        dc[0] = 0x0565;
                        dc[1] = 0x0582;
                        return 2;
                case 0xFB00: //LATIN SMALL LIGATURE FF
                        dc[0] = 'f';
                        dc[1] = 'f';
                        return 2;
                case 0xFB01: //LATIN SMALL LIGATURE FI
                        dc[0] = 'f';
                        dc[1] = 'i';
                        return 2;
                case 0xFB02: //LATIN SMALL LIGATURE FL
                        dc[0] = 'f';
                        dc[1] = 'l';
                        return 2;
                case 0xFB03: //LATIN SMALL LIGATURE FFI
                        dc[0] = 'f';
                        dc[1] = 'f';
                        dc[2] = 'i';
                        return 3;
                case 0xFB04: //LATIN SMALL LIGATURE FFL
                        dc[0] = 'f';
                        dc[1] = 'f';
                        dc[2] = 'l';
                        return 3;
                case 0xFB05: //LATIN SMALL LIGATURE LONG S T
                        //note: decompose into long s, even though we probably want a plain s
                        dc[0] = 0x017F;
                        dc[1] = 't';
                        return 2;
                case 0xFB06: //LATIN SMALL LIGATURE ST
                        dc[0] = 's';
                        dc[1] = 't';
                        return 2;
                default:
                        return 0; //not a ligature that can be decomposed for indexing purposes
        }
}

#endif
