class RdbList {

 public:

	RdbList () ;
	~RdbList () ;


	bool skipCurrentRec ( );

	bool  isExhausted        () const { return (m_listPtr >= m_listEnd); }
	void  getCurrentKey      (void *key) const { getKey(m_listPtr,(char *)key);}


	void  getKey      ( const char *rec , char *key ) const;

	char  *m_listEnd;
	char  *m_listPtr;
};



bool Rebalance_gotList (RdbList &m_list ) {
	char m_nextKey[28];
	
	int x=0;
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRec() ) {
		m_list.getCurrentKey  ( m_nextKey );
		if ( m_nextKey[0] ) x++;
	}

	return x == 0 ;
}
