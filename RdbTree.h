// Matt Wells,  Copyright, Apr 2001

// . an UNbalanced b-tree for storing keys in memory
// . The tree behanves like an AVL tree.
// . we store a -1 in the parent field for nodes that were deleted so if you
//   were to dump the nodes out unordered you'd know where the deleted nodes
//   were
// . we store "m_emptyNode" in the left kid field of deleted nodes and then
//   assign m_emptyNode to that deleted node's node # so you can re-use them 
//   in a linked-list type fashion
// . "m_minUsedNode" is the max node # ever occupied. it's used so we know the 
//   limits of a dump, if we were to dump the nodes out unordered
// . NOTE: i changed m_maxNode to m_minUnusedNode for clarity
// . "m_numNodes" is the total (used/unused) # of nodes. this can be grown
//   if we run outta room

// . RdbTree(btree) vs. a hash table 
// . 1. does not need to rehash
// . 2. does not need to sort before dump (uses getNextNode())
// . 3. takes log(N) to add/get/delete plus lotsa balancing overhead
// . 4. has 3 int32_ts overhead per node as opposed to 1 for hash table
// . 4. has 3 int32_ts overhead per node as opposed to 1 for hash table
// . 5. can do key-range lookups very quickly (hash table can't do this at all)

// TODO: use an RdbNode class so we don't have to perform as many
//       random memory accesses which are somewhat slow

// What good is just storing keys in this db? What about the data?
// You can store your data with the keys if it fits in the key size.
// However, large amounts of data per key are better stored separately because
// the key files are continually merged and sorted. Carrying around extra
// weight during these merging processes just slows things down and takes up
// more disk space. Use the "cdb" database to couple your data with your
// keys for you. It is much better suited to handling data of variable length.

#ifndef GB_RDBTREE_H
#define GB_RDBTREE_H

#include "JobScheduler.h" //for job_exit_t
#include "types.h"

class RdbList;
class BigFile;
class RdbMem;

class RdbTree {
public:
	RdbTree();
	~RdbTree();

	// . a fixedDataSize of -1 means each node has data of a variable size
	// . set maxMem to -1 for no max 
	// . returns false & sets errno if fails to alloc "maxNumNodes" nodes
	bool set(int32_t fixedDataSize, int32_t maxNumNodes,
	         int32_t maxMem, bool ownData,
	         const char *allocName,
	         const char *dbname = NULL, char keySize = 12,
	         char rdbId = -1);

	// . frees the used memory, etc.
	// . override so derivatives can free up extra header arrays
	void reset();

	// . this just makes all the nodes available for occupation (liberates)
	// . it does not free this tree's control structures
	// . returns # of occupied nodes we liberated
	int32_t clear();



	// . this will overwrite nodes with the same key
	// . returns -1 if it couldn't grab the memory or grow the table
	// . returns the node # we added it to on success
	// . don't free your data because we don't copy it!
	// . sets errno if it returns -1
	int32_t addKey(const void *key) {
		return addNode ( 0,(const char *)key,NULL,0);
	}

	int32_t addNode(collnum_t collnum, const char *key, char *data, int32_t dataSize);

	// . returns -1 if not found
	// . otherwise return the node #
	int32_t getNode(collnum_t collnum, const char *key) const;

	// . get the node whose key is >= key
	// . much much slower than getNextNode() below
	int32_t getNextNode(collnum_t collnum, const char *key) const;


	const char *getKey(int32_t node) const { return &m_keys[node*m_ks]; }

	const char *getData(collnum_t collnum, const char *key) const;
	const char *getData(int32_t node) const { return m_data[node]; }
	void setData(int32_t node, char *data) { m_data[node] = data; }

	int32_t getDataSize(int32_t node) const {
		if (m_fixedDataSize == -1) {
			return m_sizes[node];
		}
		return m_fixedDataSize;
	}

	// . get the next node # AFTER "node" by key
	// . used for dumping out the nodes ordered by their keys
	// . returns -1 on end
	int32_t getNextNode(int32_t node) const;

	int32_t getFirstNode() const;
	int32_t getLastNode() const;

	// . returns true  iff was found and deleted
	// . returns false iff not found 
	// . frees m_data[node] if freeIt is true
	void deleteNode(int32_t node, bool freeData);
	bool deleteNode(collnum_t collnum, const char *key, bool freeData);


	// . throw all the records in this range into this list
	// . used for dumping to an rdb file permanently
	// . sets list->m_lastKey to last key inserted into the list
	// . list->m_lastKey will not be valid if list is empty
	// . returns false if outta memory
	// . "antiNumRecs" is set to # of keys w/ low bit cleared (antiKeys)
	//   that were added to "list"
	bool getList(collnum_t collnum, const char *startKey, const char *endKey, int32_t minRecSizes, RdbList *list,
	             int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys) const;

	// estimate the size of the list defined by these keys
	int32_t estimateListSize(collnum_t collnum, const char *startKey, const char *endKey, char *minKey, char *maxKey) const;

	bool isSaving() const { return m_isSaving; }
	bool isWritable() const { return m_isWritable; }

	bool needsSave() const { return m_needsSave; }
	void setNeedsSave(bool needsSave) { m_needsSave = needsSave; }

	bool isLoading() const { return m_isLoading; }

	collnum_t getCollnum(int32_t node) const { return m_collnums[node]; }

	bool isEmpty(int32_t node) const { return (m_parents[node] == -2); }
	bool isEmpty() const { return (m_numUsedNodes == 0); }

	// an upper bound on the # of used nodes
	int32_t  getNumNodes() const { return m_minUnusedNode; }
	int32_t  getNumUsedNodes() const { return m_numUsedNodes; }

	int32_t  getNumAvailNodes() const { return m_numNodes - m_numUsedNodes; }
	int32_t  getNumTotalNodes() const { return m_numNodes; }

	// negative and postive counts
	int32_t getNumNegativeKeys() const { return m_numNegativeKeys; }
	int32_t getNumPositiveKeys() const { return m_numPositiveKeys; }

	int32_t getNumNegativeKeys(collnum_t collnum) const;
	int32_t getNumPositiveKeys(collnum_t collnum) const;

	// how much mem, including data, is used by this class?
	int32_t getMemAllocated() const { return m_memAllocated; }

	// . how much of the alloc'd mem is actually in use holding data
	// . includes the tree infrastructure as well as the data itself
	int32_t getMemOccupied() const { return m_memOccupied; }
	int32_t getMaxMem() const { return m_maxMem; }

	//  how much mem the tree would take if it were made into a list
	int32_t getMemOccupiedForList() const;

	// . how much mem does this tree use, not including stored data
	// . this will be the same as getMemAllocated() if fixedDataSize is 0
	int32_t getTreeOverhead() const { return m_overhead * m_numNodes; }

	// . Rdb uses this to determine when to dump this tree to disk
	// . look at % of memory occupied/allocated of max, as well as % of
	//   nodes used
	bool is90PercentFull() const {
		// . m_memOccupied is amount of alloc'd mem that data occupies
		// . now we /90 and /100 since multiplying overflowed
		return ( m_numUsedNodes/90 >= m_numNodes/100 );
	}

	int32_t getMinUnusedNode() const { return m_minUnusedNode; }

	// . load & save the tree quickly
	// . returns false on error, true otherwise
	// . sometimes sets g_errno when it returns false
	bool fastLoad(BigFile *f, RdbMem *memStack);

	// . we now optionally save with a thread
	// . when saving m_isSaving is set to true and nothing can be added
	//   to the tree, g_errno will be set to ETRYAGAIN when addNode()
	//   is called
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool fastSave(const char *dir, const char *dbname, bool useThread, void *state, void (*callback)(void *state));

	void verifyIntegrity();
	bool checkTree(bool printMsgs, bool doChainTest);
	bool fixTree();

	void disableWrites () { m_isWritable = false; }
	void enableWrites  () { m_isWritable = true ; }

	int64_t getBytesWritten() const { return m_bytesWritten; }
	int64_t getBytesRead() const { return m_bytesRead; }

	// . returns true if tree doesn't need to grow/shrink
	// . re-allocs the m_keys,m_data,m_sizes,m_leftNodes,m_rightNodes
	// . used for growing AND shrinking the table
	bool growTree(int32_t newNumNodes);

	// remove recs from tree that have invalid collnums. this is done
	// at load time. i dunno why it happens. it should never!
	void cleanTree();

	void delColl(collnum_t collnum);

private:
	static void saveWrapper(void *state);
	static void threadDoneWrapper(void *state, job_exit_t exit_type);

	// . get the node whose key is <= "key"
	int32_t getPrevNode(collnum_t collnum, const char *key) const;

	// . get the prev node # whose key is <= to key of node #i
	int32_t getPrevNode(int32_t i) const;

	// delete all nodes with keys in [startKey,endKey]
	void deleteNodes(collnum_t collnum, const char *startKey, const char *endKey, bool freeData);

	// . like getMemOccupied() above but does not include left/right/parent
	// . only includes occupied keys/sizes and the dataSizes themself
	int32_t getMemOccupiedForList2(collnum_t collnum, const char *startKey, const char *endKey, int32_t minRecSizes) const;

	bool checkTree2 ( bool printMsgs , bool doChainTest );

	// this is called by a thread
	bool fastSave_r() ;

	// used by fastSave() and fastLoad()
	int32_t fastSaveBlock_r(int fd, int32_t start, int64_t offset);
	int32_t fastLoadBlock(BigFile *f, int32_t start, int32_t totalNodes, RdbMem *stack, int64_t offset);

	void setDepths    ( int32_t bottomNode );
	int32_t rotateRight  ( int32_t pivotNode );
	int32_t rotateLeft   ( int32_t pivotNode );
	int32_t rotate       ( int32_t pivotNode , int32_t *lefts , int32_t *rights );
	int32_t computeDepth(int32_t headNode) const;

	// used by getListSize() to estiamte a list size
	int32_t getOrderOfKey(collnum_t collnum, const char *key, char *retKey) const;

	// used by getrderOfKey() (have to estimate if tree not balanced)
	int32_t getTreeDepth() const;

	// can we write to the tree?
	bool    m_isWritable;
	// . this stuff is accessed by thread an must be public
	// . cannot add to tree when saving
	bool    m_isSaving;

	// loading?
	bool    m_isLoading;

	// true if tree was modified and needs to be saved
	bool    m_needsSave;
	char  m_rdbId;
	char  m_dir[128];
	char  m_dbname[32];
	char  m_memTag[16];

	// this callback called when fastSave is complete
	void     *m_state;
	void    (* m_callback) (void *state );


	// are we responsible for freeing nodes' data
	bool    m_ownData;

	// each node/node in the tree has these datum:
	collnum_t *m_collnums; // each key now has a collection number
	char   *m_keys;         // X bytes each
	char  **m_data;         // NULL iff m_dataSize is 0
	int32_t   *m_sizes;        // NULL iff m_dataSize is 0
	int32_t   *m_left;         // left  kid of this node in the tree
	int32_t   *m_right;        // right kid of this node in the tree
	int32_t   *m_parents;      // parent of this node - for getNextNode()
	char   *m_depth;        // depth of this node
	int32_t    m_numNodes;     // how many we have, empty or full
	int32_t    m_numUsedNodes; // how many of those are used? (full)
	// negative and postive key counts
	int32_t    m_numNegativeKeys;
	int32_t    m_numPositiveKeys;
	// memory overhead per node (excluding data)
	int32_t    m_overhead;     
	// switch between picking left and right kids to replace deleted nodes
	// in order to keep the tree more balanced
	bool    m_pickRight;
	// the node at the top of the tree
	int32_t    m_headNode;
	// total mem this tree is using (including data that nodes point to)
	int32_t    m_memAllocated;
	// total amount of m_memAllocated that is occupied
	int32_t    m_memOccupied; 
	// max limit of m_memAllocated
	int32_t    m_maxMem;
	// -1 means any dataSize, otherwise, it's fixed to this
	int32_t    m_fixedDataSize;
	// node of the next available/empty node
	int32_t    m_nextNode;
	// maximum node # that was ever used at some point in time
	int32_t    m_minUnusedNode;

	const char *m_allocName;

	// so we can save the tree within a file that has other stuff
	int64_t m_bytesWritten;
	int64_t m_bytesRead;

	int32_t m_errno;
	char m_ks;

	int32_t m_corrupt;
};

#endif // GB_RDBTREE_H
