#include <gtest/gtest.h>
#include <fstream>
#include "Dir.h"

TEST(DirTest, GetNextFileNameDat) {
	std::vector<std::string> files = {"rdb001.dat", "rdb001.dat.part1", "rdb001.dat.part2"};
	for (const auto &file : files) {
		std::ofstream ofs(file);
	}

	Dir dir;
	dir.set("./");
	ASSERT_TRUE(dir.open());

	std::vector<std::string> matches;
	while (const char *collname = dir.getNextFilename("*.dat*")) {
		matches.emplace_back(collname);
	}

	std::sort(files.begin(), files.end());
	std::sort(matches.begin(), matches.end());
	EXPECT_EQ(files, matches);

	for (const auto &file : files) {
		unlink(file.c_str());
	}
}

TEST(DirTest, GetNextFileNameColl) {
	std::vector<std::string> dirs = {"testcoll.cat.0", "testcoll.dog.1", "testcoll.frog.2"};
	for (const auto &dir : dirs) {
		mkdir(dir.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXGRP);
	}

	Dir dir;
	dir.set("./");
	ASSERT_TRUE(dir.open());

	std::vector<std::string> matches;
	while (const char *collname = dir.getNextFilename("testcoll.*")) {
		matches.emplace_back(collname);
	}

	std::sort(dirs.begin(), dirs.end());
	std::sort(matches.begin(), matches.end());
	EXPECT_EQ(dirs, matches);

	for (const auto &dir : dirs) {
		rmdir(dir.c_str());
	}
}

TEST(DirTest, GetNextFileNameBlacklist) {
	std::vector<std::string> files = {"blacklist01.txt", "blacklist02.txt", "blacklist.txt"};
	for (const auto &file : files) {
		std::ofstream ofs(file);
	}

	Dir dir;
	dir.set("./");
	ASSERT_TRUE(dir.open());

	std::vector<std::string> matches;
	while (const char *collname = dir.getNextFilename("blacklist*.txt")) {
		matches.emplace_back(collname);
	}

	std::sort(files.begin(), files.end());
	std::sort(matches.begin(), matches.end());
	EXPECT_EQ(files, matches);

	for (const auto &file : files) {
		unlink(file.c_str());
	}
}
