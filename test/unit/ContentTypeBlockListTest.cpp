#include <gtest/gtest.h>
#include "ContentTypeBlockList.h"

class TestContentTypeBlockList : public ContentTypeBlockList {
public:
	TestContentTypeBlockList(const char *filename)
		: ContentTypeBlockList() {
		m_filename = filename;
	}

	using ContentTypeBlockList::load;

	bool isContentTypeBlocked(const char *str) {
		return ContentTypeBlockList::isContentTypeBlocked(str, strlen(str));
	}
};

TEST(ContentTypeBlockListTest, BlockList) {
	TestContentTypeBlockList contentTypeBlockList("blocklist/contenttype.txt");
	contentTypeBlockList.load();

	// full match
	EXPECT_TRUE(contentTypeBlockList.isContentTypeBlocked("application/font-woff"));
	EXPECT_FALSE(contentTypeBlockList.isContentTypeBlocked("application/font-woff-2"));
	EXPECT_FALSE(contentTypeBlockList.isContentTypeBlocked("naudio/"));
	EXPECT_TRUE(contentTypeBlockList.isContentTypeBlocked("audio/"));
	EXPECT_TRUE(contentTypeBlockList.isContentTypeBlocked("audio/CN"));
	EXPECT_TRUE(contentTypeBlockList.isContentTypeBlocked("audio/DAT12"));
}