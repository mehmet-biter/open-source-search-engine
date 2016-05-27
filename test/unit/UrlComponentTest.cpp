#include "gtest/gtest.h"

#include "UrlComponent.h"

TEST( UrlComponentTest, ComponentParamSingle ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1", 13, '?' );
	EXPECT_STREQ( "param1", urlComponent.getKey().c_str() );

	EXPECT_EQ( 6, urlComponent.getValueLen() );
	EXPECT_STREQ( "Value1", urlComponent.getValue() );
}

TEST( UrlComponentTest, ComponentParamDouble ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1&Param2=Value2", 13, '?' );
	EXPECT_STREQ( "param1", urlComponent.getKey().c_str() );

	EXPECT_EQ( 6, urlComponent.getValueLen() );
	EXPECT_STREQ( "Value1", urlComponent.getValue() );
}

TEST( UrlComponentTest, ComponentNormalize ) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple( "%41%42%43%44%45%46%47%48%49%4a%4b%4c%4d%4e%4f%50%51%52%53%54%55%56%57%58%59%5a",
		                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ" ),
		std::make_tuple( "%41%42%43%44%45%46%47%48%49%4A%4B%4C%4D%4E%4F%50%51%52%53%54%55%56%57%58%59%5A",
		                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ" ),
		std::make_tuple( "%61%62%63%64%65%66%67%68%69%6a%6b%6c%6d%6e%6f%70%71%72%73%74%75%76%77%78%79%7a",
		                 "abcdefghijklmnopqrstuvwxyz" ),
		std::make_tuple( "%61%62%63%64%65%66%67%68%69%6A%6B%6C%6D%6E%6F%70%71%72%73%74%75%76%77%78%79%7A",
		                 "abcdefghijklmnopqrstuvwxyz" ),
		std::make_tuple( "%30%31%32%33%34%35%36%37%38%39", "0123456789" ),
		std::make_tuple( "%2d", "-" ),
		std::make_tuple( "%2D", "-" ),
		std::make_tuple( "%2e", "." ),
		std::make_tuple( "%2E", "." ),
		std::make_tuple( "%5f", "_" ),
		std::make_tuple( "%5F", "_" ),
		std::make_tuple( "%7e", "~" ),
		std::make_tuple( "%7E", "~" ),
		std::make_tuple( "%21%40%23%24%25%5e%26%2a%28%29%2b", "%21%40%23%24%25%5E%26%2A%28%29%2B" ),
		std::make_tuple( "%e4%b8%ad%e5%9b%bD", "%E4%B8%AD%E5%9B%BD" ),
		std::make_tuple( "80%-Wool-/-20%-Viscose", "80%25-Wool-/-20%25-Viscose" )
	};

	for ( auto it = test_cases.begin(); it != test_cases.end(); ++it ) {
		std::string component( std::get<0>( *it ) );
		UrlComponent::normalize( &component );
		EXPECT_STREQ( std::get<1>( *it ), component.c_str() );
	}

}

TEST( UrlComponentTest, MatcherMatchDefault ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1", 13, '?' );

	UrlComponent::Matcher matcherLower( "param1" );
	EXPECT_TRUE( matcherLower.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherUpper( "PARAM1" );
	EXPECT_TRUE( matcherUpper.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherPrefix( "Param" );
	EXPECT_FALSE( matcherPrefix.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherLonger( "Param123" );
	EXPECT_FALSE( matcherLonger.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherExact( "Param1" );
	EXPECT_TRUE( matcherExact.isMatching( urlComponent ) );
}

TEST( UrlComponentTest, MatcherMatchCase ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1", 13, '?' );

	MatchCriteria matchCriteria = MATCH_CASE;

	UrlComponent::Matcher matcherLower( "param1", matchCriteria );
	EXPECT_FALSE( matcherLower.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherUpper( "PARAM1", matchCriteria );
	EXPECT_FALSE( matcherUpper.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherPrefix( "Param", matchCriteria );
	EXPECT_FALSE( matcherPrefix.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherLonger( "Param123", matchCriteria );
	EXPECT_FALSE( matcherLonger.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherExact( "Param1", matchCriteria );
	EXPECT_TRUE( matcherExact.isMatching( urlComponent ) );
}


TEST( UrlComponentTest, MatcherMatchPartial ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1", 13, '?' );

	MatchCriteria matchCriteria = MATCH_PARTIAL;

	UrlComponent::Matcher matcherLower( "param1", matchCriteria );
	EXPECT_TRUE( matcherLower.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherUpper( "PARAM1", matchCriteria );
	EXPECT_TRUE( matcherUpper.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherPrefix( "Param", matchCriteria );
	EXPECT_TRUE( matcherPrefix.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherLonger( "Param123", matchCriteria );
	EXPECT_FALSE( matcherLonger.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherExact( "Param1", matchCriteria );
	EXPECT_TRUE( matcherExact.isMatching( urlComponent ) );
}

TEST( UrlComponentTest, MatcherMatchCaseMatchPartial ) {
	UrlComponent urlComponent( UrlComponent::TYPE_QUERY, "Param1=Value1", 13, '?' );

	MatchCriteria matchCriteria = ( MATCH_CASE | MATCH_PARTIAL );

	UrlComponent::Matcher matcherLower( "param1", matchCriteria );
	EXPECT_FALSE( matcherLower.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherUpper( "PARAM1", matchCriteria );
	EXPECT_FALSE( matcherUpper.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherPrefix( "Param", matchCriteria );
	EXPECT_TRUE( matcherPrefix.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherLonger( "Param123", matchCriteria );
	EXPECT_FALSE( matcherLonger.isMatching( urlComponent ) );

	UrlComponent::Matcher matcherExact( "Param1", matchCriteria );
	EXPECT_TRUE( matcherExact.isMatching( urlComponent ) );
}