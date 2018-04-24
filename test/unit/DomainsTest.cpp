#include <gtest/gtest.h>
#include "Domains.h"
#include <stdio.h>
#include <string.h>


// disabled until we re-enable loading tld from file
TEST(DomainsTest, DISABLED_tld_test) {
	ASSERT_TRUE(initializeDomains("tld/tld_test.txt"));
	ASSERT_TRUE(isTLD("com",3));
	ASSERT_TRUE(isTLD("co.uk",5));
	ASSERT_FALSE(isTLD("foo.boo.goo",11));
	ASSERT_TRUE(isTLD("hemorroid",9));

	// reset data
	initializeDomains(".");
}


TEST(DomainsTest, dom_test) {
	ASSERT_TRUE(initializeDomains("tld/dom_test.txt"));
	ASSERT_STREQ(getTLD("www.ibm.com",11),"com");
	ASSERT_STREQ(getTLD("www.ibm.co.uk",13),"co.uk");
	ASSERT_STREQ(getTLD("example.com",11),"com");

	// reset data
	initializeDomains(".");
}
