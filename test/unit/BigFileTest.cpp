#include <malloc.h>
#include <gtest/gtest.h>
#include "BigFile.h"
#include "Process.h"
#include <fcntl.h>

static void createFile( BigFile *file, const char *file_name ) {
	int32_t bufSize = 1024;
	char *buf = (char *) malloc ( bufSize );
	// store stuff in there
	for ( int32_t i = 0 ; i < bufSize ; i++ ) {
		buf[i] = (char)i;
	}

	ASSERT_TRUE( file->set( ".", file_name ) );
	if ( !file->doesExist() ) {
		ASSERT_TRUE( file->open( O_RDWR | O_CREAT | O_SYNC ) );
	}
	ASSERT_TRUE( file->write(buf, bufSize, 0) );

	free( buf );
}

TEST( BigFileTest, FileRenameDestExist ) {
	BigFile file01;
	createFile( &file01, "testfile01" );

	BigFile file02;
	createFile( &file02, "testfile02" );

	EXPECT_EXIT( file02.rename( "testfile01", NULL ), ::testing::KilledBySignal(SIGABRT), "" );

	// verify files
	struct stat buffer;
	EXPECT_TRUE(stat("testfile01", &buffer) == 0);
	EXPECT_TRUE(stat("testfile02", &buffer) == 0);

	// remove files
	file01.unlink();
	file02.unlink();
	unlink(Process::getAbortFileName());
}

TEST( BigFileTest, FileRenameDestNotExist ) {
	BigFile file01;
	createFile( &file01, "testfile01" );

	ASSERT_TRUE( file01.rename( "testfile02", NULL ) );

	// verify files
	struct stat buffer;
	EXPECT_FALSE(stat("testfile01", &buffer) == 0);
	EXPECT_TRUE(stat("testfile02", &buffer) == 0);

	// remove files
	file01.unlink();
}
