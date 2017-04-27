// Matt Wells, copyright Jul 2004

// . class used to hold the top scoring search results
// . used by Msg38 to get cluster info for each TopNode
// . used by Msg39 to serialize into a reply

#ifndef GB_TOPTREE_H
#define GB_TOPTREE_H

#include "RdbTree.h"


class TopNode {
 public:
	//unsigned char  m_bscore ; // bit #6(0x40) is on if has all explicitly
	// do not allow a higher-tiered node to outrank a lower that has
	// bit #6 set, under any circumstance

	char           m_depth    ;

	// Msg39 now looks up the cluster recs so we can do clustering
	// really quick on each machine, assuming we have a full split and the
	// entire clusterdb is in our local disk page cache.
	char     m_clusterLevel;
	key96_t    m_clusterRec;

	// no longer needed, Msg3a does not need, it has already
	//unsigned char  m_tier     ;
	float          m_score    ;
	int64_t      m_docId;
	unsigned     m_flags; //from Docid2FlagsAndSiteMap

	// option for using int scores
	int32_t m_intScore;
	
	// clustering info
	//int32_t           m_kid      ; // result from our same site below us
	//uint32_t  m_siteHash ;
	//uint32_t  m_contentHash ;
	//int32_t           m_rank        ;

	// the lower 64 bits of the cluster rec, used by Msg51, the new
	// class for doing site clustering
	//uint64_t       m_clusterRec;

	// . for getting similarity between titleRecs
	// . this is so big only include if we need it
	//int32_t           m_vector [ VECTOR_SIZE ];

	// tree info, indexes into m_nodes array
	int32_t m_parent;
	int32_t m_left;   // kid
	int32_t m_right;  // kid

	// so we can quickly remove its scoring info from the scoreinfo
	// buf and replace with new docid's scoring info
	//int64_t m_scoreInfoBufOffset;

	//int64_t getDocId ( );

	//int64_t getDocIdForMsg3a ( );
};

class TopTree {
 public:
	TopTree();
	~TopTree();
	// free mem
	void reset();
	// pre-allocate memory
	bool setNumNodes ( int32_t docsWanted , bool doSiteClustering );
	// . add a node
	// . get an empty first, fill it in and call addNode(t)
	int32_t getEmptyNode ( ) { return m_emptyNode; }
	// . you can add a new node
	// . it will NOT overwrite a node with same bscore/score/docid
	// . it will NOT add if bscore/score/docid < m_tail node
	//   otherwise it will remove m_tail node if 
	//   m_numNodes == m_numUsedNodes
	bool addNode ( TopNode *t , int32_t tnn );

	int32_t getLowNode  ( ) { return m_lowNode ; }
	// . this is computed and stored on demand
	// . WARNING: only call after all nodes have been added!
	int32_t getHighNode ( ) ;

	int32_t getPrev ( int32_t i );
	int32_t getNext ( int32_t i );

	bool hasDocId ( int64_t d );
	void logTreeData(int32_t loglevel);

	TopNode *getNode ( int32_t i ) { return &m_nodes[i]; }
	bool nodesIsNull() const { return m_nodes==NULL; }
	int32_t getNumNodes() const { return m_numNodes; }
	int32_t getNumUsedNodes() const { return m_numUsedNodes; }
	int32_t getNumDocsWanted() const { return m_docsWanted; }


	bool  m_useIntScores;

private:
	int32_t  m_docsWanted;
	bool checkTree ( bool printMsgs ) ;
	int32_t computeDepth ( int32_t i ) ;

	bool  m_doSiteClustering;

	// ptr to the mem block
	TopNode *m_nodes;
	int32_t     m_allocSize;
	int32_t m_numUsedNodes;
	// total count
	int32_t m_numNodes;
	// the top of the tree
	int32_t m_headNode;

	// . always keep track of the high and low nodes
	// . IndexTable.cpp likes to replace the low-scoring tail often 
	// . Msg39.cpp likes to print out starting at the high-scorer
	// . these are indices into m_nodes[] array
	int32_t m_lowNode;
	int32_t m_highNode;

	// use this to set "t" in call to addNode(t)
	int32_t m_emptyNode;

	bool m_pickRight;

	int32_t  m_cap;
	float m_vcount;
	float m_partial;
	int64_t  m_ridiculousMax;
	bool  m_kickedOutDocIds;
	//int64_t m_lastKickedOutDocId;
	int32_t  m_domCount[256];
	// the node with the minimum "score" for that domHash
	int32_t  m_domMinNode[256];

	// an embedded RdbTree for limiting the storing of keys to X
	// keys per domHash, where X is usually "m_ridiculousMax"
	RdbTree m_t2;

	void deleteNode  ( int32_t i , uint8_t domHash ) ;
	void setDepths   ( int32_t i ) ;
	int32_t rotateLeft  ( int32_t i ) ;
	int32_t rotateRight ( int32_t i ) ;
};

#endif // GB_TOPTREE_H
