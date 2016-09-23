#include <gtest/gtest.h>

#include "Process.h"

TEST(ProcessTest, Abort) {
	EXPECT_EXIT(g_process.shutdownAbort(), ::testing::KilledBySignal(SIGABRT), "");

	// verify file is created
	struct stat buffer;
	EXPECT_TRUE(stat(Process::getAbortFileName(), &buffer) == 0);

	// remove file
	unlink(Process::getAbortFileName());

	/// @todo ALC verify/remove core
}
