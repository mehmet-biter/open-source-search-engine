#include "gb-include.h"

#include "Mem.h"
#include "UCPropTable.h"
#include "Unicode.h"

bool loadUnidataProps(char *s, void (*handler)(u_int32_t, char**, u_int32_t));

void handleUnicodeData(u_int32_t, char **col, u_int32_t colCount);
void handleDerivedCoreProps(u_int32_t, char **col, u_int32_t colCount);
void handleDerivedNormalizationProps(u_int32_t, char **col, u_int32_t colCount);
void handlePropList(u_int32_t, char **col, u_int32_t colCount);
void handleScripts(u_int32_t, char **col, u_int32_t colCount);
void decomposeHangul();



// static int g_decompCount = 0;
static int g_canonicalDecompCount = 0;
static int g_excludeCount = 0;


bool g_recoveryMode = false;
int32_t g_recoveryLevel = 0;

int main(int argc, char **argv) {
	// Avoid SEGV

	if ( ! g_log.init( "foo.log" )        ) {
		fprintf (stderr,"db: Log file init failed.\n" ); exit( 1 ); }
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); exit(1); }
	// . hashinit() calls srand() w/ a fixed number
	// . let's mix it up again
	srand ( time(NULL) );

	UCProps props = 0;
	g_ucProps.setValue(0, &props);

	// Download data from:
	//   ftp://ftp.unicode.org/Public/UNIDATA/

	loadUnidataProps("UNIDATA/DerivedNormalizationProps.txt", handleDerivedNormalizationProps);
	loadUnidataProps("UNIDATA/UnicodeData.txt", handleUnicodeData);

	decomposeHangul(); // set up algorithmic hangul decomps
 	printf("%d canonical deompositions\n", g_canonicalDecompCount);
	printf("%d code points excluded\n", g_excludeCount);

	loadUnidataProps("UNIDATA/DerivedCoreProperties.txt", handleDerivedCoreProps);
	loadUnidataProps("UNIDATA/PropList.txt", handlePropList);
	loadUnidataProps("UNIDATA/Scripts.txt", handleScripts);
	
	printf("lower case map size: %lu\n", g_ucLowerMap.getSize());
	saveUnicodeTable(&g_ucLowerMap, "ucdata/lowermap.dat");
	printf("upper case map size: %lu\n", g_ucUpperMap.getSize());
	saveUnicodeTable(&g_ucUpperMap, "ucdata/uppermap.dat");
	printf("properties size: %lu\n", g_ucProps.getSize());
	saveUnicodeTable(&g_ucProps, "ucdata/properties.dat");
	printf("scripts size: %lu\n", g_ucScripts.getSize());
	saveUnicodeTable(&g_ucScripts, "ucdata/scripts.dat");

	// JAB: we now have Kompatible and Canonical decompositions
	saveKDecompTable();

	g_mem.printMem();

	if (loadUnicodeTable(&g_ucUpperMap,"ucdata/uppermap.dat") &&
	    loadUnicodeTable(&g_ucLowerMap,"ucdata/lowermap.dat") &&
	    loadUnicodeTable(&g_ucProps,"ucdata/properties.dat") &&
	    loadUnicodeTable(&g_ucScripts,"ucdata/scripts.dat") &&
	    // JAB: we now have Kompatible and Canonical decompositions
	    loadDecompTables()){
		printf("tables reloaded successfully\n\n");

		printf("lower case map size: %lu\n", g_ucLowerMap.getSize());
		printf("upper case map size: %lu\n", g_ucUpperMap.getSize());
		printf("properties size: %lu\n", g_ucProps.getSize());
		printf("scripts size: %lu\n", g_ucScripts.getSize());
		printf("Kompat Decomp size: %lu\n", g_ucKDIndex.getSize());
		exit(0);
	}
}

// ftp://ftp.unicode.org/Public/3.0-Update/UnicodeData-3.0.0.html
void handleUnicodeData(u_int32_t line, char **col, u_int32_t colCount) {

	UChar32 codePoint = strtol(col[0], NULL, 16);

// 	if ((colCount < 14) || (codePoint == 0)){
// 		printf("line %"INT32": no data (%"INT32" cols)\n", line, colCount);
// 		return;
// 	}
	char *name = col[1];
	char *category = col[2];
	char *decompStr = col[5];
	UChar32 ucMapping = strtol(col[12],NULL, 16);
	UChar32 lcMapping = strtol(col[13],NULL, 16);

	UCProps props = ucProperties(codePoint);
	if (category[0] == 'L') props |= UC_ALPHA | UC_WORDCHAR;
	else if (category[0] == 'N') props |= UC_DIGIT | UC_WORDCHAR;
	else if (category[0] == 'Z') props |= UC_WHITESPACE;
	if (props)
		g_ucProps.setValue(codePoint, &props);
	
	if (lcMapping) 
		g_ucLowerMap.setValue(codePoint, (void*)&lcMapping);
	if (ucMapping) 
		g_ucUpperMap.setValue(codePoint, (void*)&ucMapping);

	if (decompStr && decompStr[0]){
		
		u_char decompCount = 0;
		UChar32 decomp[32];
		bool kompat = false;
		// Get decomposition
		char *p = decompStr;
		int decompLen = gbstrlen(decompStr);
		while (p < decompStr+decompLen) {
			char *pend = p;
			while (*pend && *pend != ' ') pend++;
			*pend = '\0';
			if (p[0] == '<') kompat = true;
			else{
				decomp[decompCount++] = strtol(p, NULL, 16);
			}
			p = pend+1;
		}

//  		printf ("Code Point U+%04"XINT32", %s: %s (%d chars)\n",
//  			codePoint, name, kompat?"(Kompatable)":"", decompCount);
// 		g_decompCount++;
// 		if (decompStr[0] != '<')
		bool fullComp=false;
		if (!kompat && !(props & UC_COMP_EX)) {
			// set up canonical combining table
			g_canonicalDecompCount++;
// 			printf("%4x:", codePoint);
// 			for (int i = 0; i<decompCount;i++)
// 				printf(" %4x", decomp[i]);
// 			printf("\n");
			fullComp = true;
		}
		setKDValue(codePoint, decomp, decompCount, fullComp);
	}
}

void handlePropList(u_int32_t line, char **col, u_int32_t colCount) {
	//printf("Line %"INT32": ", line);
	//for (u_int32_t i=0;i<colCount;i++)
	//	printf("'%s' ", col[i]);
	//printf("\n");
	char *range = NULL;
	UChar32 codePointStart = strtol(col[0], &range, 16);
	UChar32 codePointEnd = codePointStart;
	if (range && range[0] == '.' && range[1] == '.')
		codePointEnd = strtol(range+2, NULL, 16);
	for (UChar32 c = codePointStart ; c <= codePointEnd ; c++) {
		//printf("U+%04x ", c);
		// get current props, if any
		UCProps props = ucProperties(c);
		//void *p = g_ucProps.getValue(c);
		//if (p) props = *(u_char*)p;
		if (!strncmp(col[1], "Ideographic", 11))
			props |= UC_IDEOGRAPH | UC_WORDCHAR;
		else if (!strncmp(col[1], "Unified_Ideograph", 17))
			props |= UC_IDEOGRAPH | UC_WORDCHAR;
		else if (!strncmp(col[1], "White_Space", 11))
			props |= UC_WHITESPACE;

		if (props)
			g_ucProps.setValue(c, &props);
	}
	//printf("\n");
	
}

void handleDerivedCoreProps(u_int32_t line, char **col, u_int32_t colCount) {
	//printf("Line %"INT32": ", line);
	//for (u_int32_t i=0;i<colCount;i++) 
	//	printf("'%s' ", col[i]);
	//printf("\n");
	char *range = NULL;
	UChar32 codePointStart = strtol(col[0], &range, 16);
	UChar32 codePointEnd = codePointStart;
	if (range && range[0] == '.' && range[1] == '.')
		codePointEnd = strtol(range+2, NULL, 16);
	for (UChar32 c = codePointStart ; c <= codePointEnd ; c++) {
		//printf("U+%04x ", c);
		// get current props, if any
		UCProps props = ucProperties(c);
		if (!strncmp(col[1], "Alphabetic", 10))
			props |= UC_ALPHA | UC_WORDCHAR;
		else if (!strncmp(col[1], "Default_Ignorable_Code_Point", 28))
			props |= UC_IGNORABLE;
		else if (!strncmp(col[1], "Lowercase", 9))
			props |= UC_LOWER | UC_WORDCHAR;
		else if (!strncmp(col[1], "Uppercase", 9))
			props |= UC_UPPER | UC_WORDCHAR;
		else if (!strncmp(col[1], "Grapheme_Extend", 15))
			props |= UC_WORDCHAR;
		if (props)
			g_ucProps.setValue(c, &props);
// 		if (c == ' ' && (props&UC_WORDCHAR)) 
// 			printf("Yow: line %"INT32"\n", line);
// 		if (c == 0 && props)
// 			printf("!!!\nHey: line %"INT32"!!!\n\n", line);
	}
	//printf("\n");
	
}

void handleDerivedNormalizationProps(u_int32_t line, char **col, 
				     u_int32_t colCount) {
	//printf("Line %"INT32": ", line);
	//for (u_int32_t i=0;i<colCount;i++) 
	//	printf("'%s' ", col[i]);
	//printf("\n");
	char *range = NULL;
	UChar32 codePointStart = strtol(col[0], &range, 16);
	UChar32 codePointEnd = codePointStart;
	if (range && range[0] == '.' && range[1] == '.')
		codePointEnd = strtol(range+2, NULL, 16);
	for (UChar32 c = codePointStart ; c <= codePointEnd ; c++) {
		//printf("U+%04x ", c);
		// get current props, if any
		UCProps props = ucProperties(c);

		if (!strncmp(col[1], "NFKC_QC", 7))
			props |= UC_NFKC_QC_NO;
		else if (!strncmp(col[1], "Full_Composition_Exclusion", 26)){
			g_excludeCount++;
			props |= UC_COMP_EX;
			//printf("Excluding %4x props: %04x\n", c, props);
		}
		
		if (props) g_ucProps.setValue(c, &props);
	}
	//printf("\n");
	
}

void handleScripts(u_int32_t, char **col, u_int32_t colCount){
	char *range = NULL;
	UChar32 codePointStart = strtol(col[0], &range, 16);
	UChar32 codePointEnd = codePointStart;
	if (range && range[0] == '.' && range[1] == '.')
		codePointEnd = strtol(range+2, NULL, 16);
	for (UChar32 c = codePointStart ; c <= codePointEnd ; c++) {
		UCProps props = ucProperties(c);
		//void *p = g_ucProps.getValue(c);
		//if (p) props = *(u_char*)p;
		UCScript s = ucScriptCommon;
		for (int j=0; j < ucScriptNumScripts; j++) {
			if (!strcmp(col[1], g_ucScriptNames[j])){
				s = j;
				g_ucScripts.setValue(c, &j);
			}
		}
		if (s == ucScriptThai) props |= UC_THAI;
		else if (s == ucScriptHiragana) props |= UC_HIRAGANA;
		else if (s == ucScriptKatakana) props |= UC_KATAKANA;
		else if (s == ucScriptKatakana_Or_Hiragana) 
			props |= UC_KATAKANA|UC_HIRAGANA;
		if (props)
			g_ucProps.setValue(c, &props);
	}

}

bool loadUnidataProps(char *filename, 
		      void (*handler)(u_int32_t, char**, u_int32_t)) {
	printf("Loading %s\n", filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		printf("Error opening %s: %s\n",filename, strerror(errno));
		return false;
	}

	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	// JAB: Mem.h cores on use of malloc()
	char *buf = (char*)mmalloc(fsize+1, "loadUnidataProps");
	if (!buf){
		printf("Error allocating %d bytes for %s\n", 
		       fsize+1, filename);
		return false;
	}
	rewind(fp);
	size_t nread = fread(buf, 1, fsize, fp);
	//printf("Read %d bytes\n", nread);
	buf[nread] = '\0';
	fclose(fp);

	char *lineStart = buf;
	char *lineEnd = lineStart;
	u_int32_t line = 0;

	while ((lineStart < buf+nread) && *lineStart) {
		while (*lineEnd && *lineEnd != '\n') lineEnd++;

		char *tokStart = lineStart;
		u_int32_t colCount = 0;

		bool lineDone = false;
		char *col[16];

		while (tokStart < lineEnd )  {
			// skip leading whitespace
			while (*tokStart == ' ') tokStart++;
			char *tokEnd = tokStart;

			while (tokEnd < lineEnd && 
			       *tokEnd != ';' &&
			       *tokEnd != '#')tokEnd++;


			if ( *tokEnd == '#' )
				lineDone = true;
			char *trim = tokEnd-1;
			*tokEnd++ = '\0';
			while (trim > tokStart && 
			       (*trim == ' ' ||
				*trim == '\t')) {
				*trim-- = '\0';

			}

			//printf("Line %"INT32" col %"INT32" Token: '%s'\n",
			//       line, col, tokStart);
			col[colCount] = tokStart;
			
			tokStart = tokEnd;
			colCount++;
			if (lineDone) break;			
		}
		//if (col != 14)printf("uh oh: %"INT32"\n", col);
		//eol:
		if (colCount && col[0][0] != 0){
			handler(line, col, colCount);
			
		}
		// skip newline
		lineEnd++;
		lineStart = lineEnd;
		line++;
	}
	free(buf);
	return true;
}

void decomposeHangul() {
	for (UChar32 sIndex = 0; sIndex < ucSCount ; sIndex++) {
		int tIndex = sIndex % ucTCount;
		int first, second;
		if (tIndex != 0) { // triple
			first = (int)(ucSBase + sIndex - tIndex);
			second = (int) (ucTBase + tIndex);
		}
		else {
			first = (int) (ucLBase + sIndex / ucNCount);
			second = (int) (ucVBase + (sIndex % ucNCount) 
					/ ucTCount);
		}
		int value = sIndex + ucSBase ;
		//printf("value: %4x, first: %4x second %4x\n",
		//       value, first, second);
		UChar32 decomp[2];
		decomp[0] = first;
		decomp[1] = second;
		g_canonicalDecompCount++;
		setKDValue(value, decomp, 2, true);
	}
}
