#include <gtest/gtest.h>
#include "XmlDoc.h"
#include "GigablastTestUtils.h"

class XmlDocTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
		g_conf.m_noInMemoryPosdbMerge = true;
	}

	static void TearDownTestCase() {
		g_conf.m_noInMemoryPosdbMerge = m_savedMergeConf;
	}

	void SetUp() {
		GbTest::initializeRdbs();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}

	static bool m_savedMergeConf;
};

bool XmlDocTest::m_savedMergeConf = g_conf.m_noInMemoryPosdbMerge;

static void initializeDocForPosdb(XmlDoc *xmlDoc, const char *url, char *content) {
	CollectionRec *cr = g_collectiondb.getRec(static_cast<collnum_t >(0));

	xmlDoc->setCollNum(cr->m_coll);
	xmlDoc->setFirstUrl(url);

	xmlDoc->m_oldDocValid = true;
	xmlDoc->m_oldDoc = NULL;

	xmlDoc->m_docIdValid = true;
	xmlDoc->m_docId = Titledb::getProbableDocId(url);

	xmlDoc->m_ipValid = true;
	xmlDoc->m_ip = atoip("127.0.0.2");

	xmlDoc->m_useFakeMime = true;
	xmlDoc->m_httpReplyValid = true;
	xmlDoc->m_httpReplyAllocSize = strlen(content) + 1;
	xmlDoc->m_httpReplySize = xmlDoc->m_httpReplyAllocSize;
	xmlDoc->m_httpReply = static_cast<char*>(mmalloc(xmlDoc->m_httpReplyAllocSize, "httprep"));
	memcpy(xmlDoc->m_httpReply, content, xmlDoc->m_httpReplyAllocSize);

	xmlDoc->m_downloadStatusValid = true;
	xmlDoc->m_downloadStatus = 0;

	xmlDoc->m_useRobotsTxt = false;
	xmlDoc->m_useClusterdb = false;
	xmlDoc->m_useLinkdb = false;
	xmlDoc->m_useSpiderdb = false;
	xmlDoc->m_useTitledb = false;
	xmlDoc->m_useTagdb = false;

	xmlDoc->m_linkInfo1Valid = true;
	xmlDoc->ptr_linkInfo1 = NULL;

	xmlDoc->m_siteValid = true;
	xmlDoc->ptr_site = const_cast<char*>(url);

	xmlDoc->m_tagRecValid = true;

	xmlDoc->m_siteNumInlinksValid = true;
	xmlDoc->m_siteNumInlinks = 0;

	xmlDoc->m_sreqValid = true;
	xmlDoc->m_sreq.setFromInject(url);

	xmlDoc->m_versionValid = true;
	xmlDoc->m_version = TITLEREC_CURRENT_VERSION;

	// debug info
	xmlDoc->m_storeTermListInfo = true;
}

typedef std::pair<rdbid_t, const char*> keypair_t;

static std::vector<keypair_t> parseMetaList(const char *metaList, int32_t metaListSize) {
	std::vector<keypair_t> result;

	const char *p    = metaList;
	const char *pend = metaList + metaListSize;
	for (; p < pend;) {
		// get rdbId
		rdbid_t rdbId = (rdbid_t)(*p & 0x7f);
		p++;

		// key size
		int32_t ks = getKeySizeFromRdbId(rdbId);

		// get key
		const char *key = p;
		p += ks;

		// . if key is negative, no data is present
		// . the doledb key is negative for us here
		int32_t ds = KEYNEG(key) ? 0 : getDataSizeFromRdbId(rdbId);

		// if datasize variable, read it in
		if (ds == -1) {
			// get data size
			ds = *(int32_t *)p;

			// skip data size int32_t
			p += 4;
		}

		// skip data if not zero
		p += ds;

		result.push_back(std::make_pair(rdbId,key));
	}

	return result;
}

static bool posdbFindRecord(const std::vector<keypair_t> &metaListKeys, int64_t docId, int64_t termId, bool isDel = false) {
	for (auto it = metaListKeys.begin(); it != metaListKeys.end(); ++it) {
		if (it->first == RDB_POSDB && Posdb::getDocId(it->second) == docId && Posdb::getTermId(it->second) == termId && KEYNEG(it->second) == isDel) {
			return true;
		}
	}

	return false;
}

static int64_t hashWord(const char *prefix, const char *word) {
	uint64_t prefixHash = hash64(prefix, strlen(prefix));
	return (hash64(hash64Lower_utf8(word), prefixHash) & TERMID_MASK);
}

static int64_t hashWord(const char *word) {
	return (hash64Lower_utf8(word) & TERMID_MASK);
}

TEST_F(XmlDocTest, PosdbGetMetaListNewDoc) {
	const char *url = "http://www.example.test/index.html";
	char contentNew[] = "<html><head><title>my title</title></head><body>new document</body></html>";

	XmlDoc xmlDocNew;
	initializeDocForPosdb(&xmlDocNew, url, contentNew);

	xmlDocNew.getMetaList(false);
	auto metaListKeys = parseMetaList(xmlDocNew.m_metaList, xmlDocNew.m_metaListSize);

	// make sure positive special key is there (to clear out existing negative special key)
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, false));
	EXPECT_FALSE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, true));

	// make sure title & body text is indexed

	// title
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "my"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "title"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "mytitle"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("my"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("mytitle"), false));

	// body
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("new"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("document"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("newdocument"), false));

	/// @todo ALC add other terms
}

TEST_F(XmlDocTest, PosdbGetMetaListChangedDoc) {
	const char *url = "http://www.example.test/index.html";
	char contentOld[] = "<html><head><title>my title</title></head><body>old document</body></html>";
	char contentNew[] = "<html><head><title>my title</title></head><body>new document</body></html>";

	XmlDoc *xmlDocOld = new XmlDoc();
	mnew(xmlDocOld, sizeof(*xmlDocOld), "XmlDoc");
	initializeDocForPosdb(xmlDocOld, url, contentOld);

	XmlDoc xmlDocNew;
	initializeDocForPosdb(&xmlDocNew, url, contentNew);

	xmlDocNew.m_oldDocValid = true;
	xmlDocNew.m_oldDoc = xmlDocOld;
	xmlDocNew.getMetaList(false);
	auto metaListKeys = parseMetaList(xmlDocNew.m_metaList, xmlDocNew.m_metaListSize);

	// make sure no special key is inserted (positive or negative)
	EXPECT_FALSE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, false));
	EXPECT_FALSE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, true));

	// make sure title & body text is indexed (with difference between old & new document deleted)

	// title
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "my"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "title"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "mytitle"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("my"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("mytitle"), false));

	// body
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("new"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("document"), false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("newdocument"), false));

	// deleted terms
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("old"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("olddocument"), true));

	/// @todo ALC add other terms
}

TEST_F(XmlDocTest, PosdbGetMetaListDeletedDoc) {
	const char *url = "http://www.example.test/index.html";
	char contentOld[] = "<html><head><title>my title</title></head><body>old document</body></html>";
	char contentNew[] = "";

	XmlDoc *xmlDocOld = new XmlDoc();
	mnew(xmlDocOld, sizeof(*xmlDocOld), "XmlDoc");
	initializeDocForPosdb(xmlDocOld, url, contentOld);

	XmlDoc xmlDocNew;
	initializeDocForPosdb(&xmlDocNew, url, contentNew);
	xmlDocNew.m_deleteFromIndex = true;

	xmlDocNew.m_oldDocValid = true;
	xmlDocNew.m_oldDoc = xmlDocOld;
	xmlDocNew.getMetaList(false);
	auto metaListKeys = parseMetaList(xmlDocNew.m_metaList, xmlDocNew.m_metaListSize);

	// make sure negative special key is there (to indicate document is deleted)
	EXPECT_FALSE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, false));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, POSDB_DELETEDOC_TERMID, true));

	// make sure title & body text from old document is deleted

	// title
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "my"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "title"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title", "mytitle"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("my"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("title"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("mytitle"), true));

	// body
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("old"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("document"), true));
	EXPECT_TRUE(posdbFindRecord(metaListKeys, xmlDocNew.m_docId, hashWord("olddocument"), true));

	/// @todo ALC add other terms
}