#include "BigFile.h"
#include "RdbIndex.h"
#include "Posdb.h"
#include "Log.h"
#include <libgen.h>
#include <stdlib.h>
#include <algorithm>
#include <unordered_set>
#include <assert.h>
#include <RdbIndexQuery.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH RDB\n", argv0);
	fprintf(stdout, "Validate index for RDB in PATH\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
}

static bool createTestData(BigFile &bigFile, RdbBase *base, std::vector<uint64_t> &testData) {
	if (!bigFile.open(O_RDWR|O_CREAT)) {
		logf(LOG_WARN, "Could not create test file[%s] for writing", bigFile.getFilename());
		return 1;
	}

	std::unordered_set<uint64_t> docIds;

	auto rdbDocIds = base->getTreeIndex()->getDocIds();
	for (auto it = rdbDocIds->begin(); it != rdbDocIds->end(); ++it) {
		auto result = docIds.insert(*it);
		if (result.second) {
			uint64_t key = (*it << 24) | base->getNumFiles();
			testData.push_back(key);
		}
	}

	for (int32_t i = base->getNumFiles() - 1; i >= 0; --i) {
		auto rdbDocIds = base->getIndex(i)->getDocIds();
		for (auto it = rdbDocIds->begin(); it != rdbDocIds->end(); ++it) {
			auto result = docIds.insert(*it);
			if (result.second) {
				uint64_t key = ((*it << 24) | i);
				testData.push_back(key);
			}
		}
	}

	std::random_shuffle(testData.begin(), testData.end());

	int64_t offset = 0;
	size_t docid_count = 0;

	docid_count = testData.size();
	bigFile.write(&docid_count, sizeof(docid_count), offset);
	if (g_errno) {
		logError("Failed to write to %s (docid_count): %s", bigFile.getFilename(), mstrerror(g_errno));
		return false;
	}

	offset += sizeof(docid_count);

	bigFile.write(&testData[0], docid_count * sizeof(testData[0]), offset);
	if (g_errno) {
		logError("Failed to write to %s (docids): %s", bigFile.getFilename(), mstrerror(g_errno));
		return false;
	}

	return true;
}

static bool initializeTestData(RdbBase *base, const char *currentPath, const char *dbname, std::vector<uint64_t> &testData) {
	// initialize test data (if not present)
	BigFile bigFile;
	char testFile[255];
	snprintf(testFile, sizeof(testFile), "%s_idx_test.dat", dbname);
	bigFile.set(currentPath, testFile);

	if (!bigFile.doesExist()) {
		return createTestData(bigFile, base, testData);
	}

	if (bigFile.open(O_RDONLY)) {
		int64_t offset = 0;
		size_t docid_count = 0;

		// first 8 bytes are the size of the DATA file we're indexing
		bigFile.read(&docid_count, sizeof(docid_count), offset);
		if (g_errno) {
			logError("Had error reading offset=%" PRId64" from %s: %s", offset, bigFile.getFilename(), mstrerror(g_errno));
			return false;
		}

		offset += sizeof(docid_count);
		testData.resize(docid_count);

		bigFile.read(&testData[0], docid_count * sizeof(testData[0]), offset);
		if (g_errno) {
			logError("Had error reading offset=%" PRId64" from %s: %s", offset, bigFile.getFilename(), mstrerror(g_errno));
			return false;
		}
	}

	return true;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "--h") == 0 || strcmp(argv[1], "--help") == 0 ) {
		print_usage(argv[0]);
		return 1;
	}

	char tmpPath[PATH_MAX];

	// collection name
	char collName[255];
	realpath(argv[1], tmpPath);
	strcpy(collName, strrchr(tmpPath, '/') + 1);

	// gb path
	char basePath[PATH_MAX];
	strcat(tmpPath, "/../");
	realpath(tmpPath, basePath);
	size_t basePathLen = strlen(basePath);
	if (basePath[basePathLen] != '/') {
		strcat(basePath, "/");
	}

	// current dir
	realpath(argv[0], tmpPath);
	char currentPath[PATH_MAX];
	strcpy(currentPath, dirname(tmpPath));

	logf(LOG_DEBUG, "basepath=%s collName=%s currentPath=%s", basePath, collName, currentPath);

	// initialize library
	g_mem.init();
	hashinit();

	g_hostdb.init(-1, NULL, false, false, basePath);
	g_conf.init(NULL);
	g_conf.m_noInMemoryPosdbMerge = true;

	g_collectiondb.loadAllCollRecs();

	Rdb *rdb = NULL;
	const char *dbname = argv[2];
	if (strcmp(dbname, "posdb") == 0) {
		g_posdb.init();
		rdb = g_posdb.getRdb();
	} else {
		logError("Unsupported db\n");
		return 1;
	}

	g_collectiondb.addRdbBaseToAllRdbsForEachCollRec();

	// try to get collnum from path
	collnum_t collNum = strtol(strrchr(collName, '.') + 1, NULL, 10);
	RdbBase *base = rdb->getBase(collNum);

	// get test data
	std::vector<uint64_t> testData;
	if (!initializeTestData(base, currentPath, dbname, testData)) {
		logError("Unable to initialize test data\n");
		return 1;
	}

	logf(LOG_DEBUG, "Starting test with %zu entries", testData.size());
	uint64_t start = gettimeofdayInMicroseconds();

	RdbIndexQuery rdbIndexQuery(base);
	for (auto it = testData.begin(); it != testData.end(); ++it) {
		assert(rdbIndexQuery.getFilePos(*it >> RdbBase::s_docIdFileIndex_docIdOffset) == static_cast<int32_t>(*it & RdbBase::s_docIdFileIndex_filePosMask));
	}

	uint64_t diff = gettimeofdayInMicroseconds() - start;
	logf(LOG_DEBUG, "Ending test after %ld ms (%f us)", diff / 1000, ((double)diff / testData.size()));
	return 0;
}
