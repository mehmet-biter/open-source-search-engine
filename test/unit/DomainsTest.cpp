#include <gtest/gtest.h>
#include "Domains.h"
#include <stdio.h>
#include <string.h>


TEST(DomainsTest, tld_test) {
	FILE *fp=fopen("tlds.txt","w");
	ASSERT_TRUE(fp!=NULL);
	fprintf(fp,"#test line 1\n");
	fprintf(fp,"com\n");
	fprintf(fp,"co.uk\n");
	fprintf(fp,"boo\n");
	fprintf(fp,"\n");
	fprintf(fp,"hemorroid\n");
	fclose(fp);
	
	ASSERT_TRUE(initializeDomains("."));
	ASSERT_TRUE(isTLD("com",3));
	ASSERT_TRUE(isTLD("co.uk",5));
	ASSERT_FALSE(isTLD("foo.boo.goo",11));
	ASSERT_TRUE(isTLD("hemorroid",9));
}


TEST(DomainsTest, dom_test) {
	FILE *fp=fopen("tlds.txt","w");
	ASSERT_TRUE(fp!=NULL);
	fprintf(fp,"com\n");
	fprintf(fp,"co.uk\n");
	fclose(fp);
	
	ASSERT_TRUE(initializeDomains("."));
	ASSERT_STREQ(getTLD("www.ibm.com",11),"com");
	ASSERT_STREQ(getTLD("www.ibm.co.uk",13),"co.uk");
	ASSERT_STREQ(getTLD("example.com",11),"com");
}
