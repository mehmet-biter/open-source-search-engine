#include "gb-include.h"

#include "RdbIndex.h"
#include "BigFile.h"
#include "Titledb.h"	// For MAX_DOCID
#include "Process.h"
#include "BitOperations.h"
#include "Conf.h"

RdbIndex::RdbIndex() {
	reset ( );
}

// dont save index on deletion!
RdbIndex::~RdbIndex() {
	reset();
}

void RdbIndex::set ( const char *dir, const char *indexFilename) {
	logTrace( g_conf.m_logTraceRdbIndex, "BEGIN. dir [%s], indexFilename [%s]", dir, indexFilename );

	reset();
	m_file.set ( dir , indexFilename );
}


bool RdbIndex::close ( bool urgent ) {
	bool status = true;
	if ( m_needToWrite ) {
		status = writeIndex();
	}

	// clears and frees everything
	if ( ! urgent ) {
		reset ();
	}

	return status;
}


void RdbIndex::reset ( ) {
	//@todo: IMPLEMENT!
//	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);

	m_generatingIndex = false;
	m_lastDocId 		= MAX_DOCID+1;
	m_needToWrite     	= false;

	//@@@ free mem here

	m_file.reset();
}


bool RdbIndex::writeIndex() {
	logTrace( g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	if ( g_conf.m_readOnlyMode ) {
		logTrace( g_conf.m_logTraceRdbIndex, "END. Read-only mode, not writing index. filename [%s]. Returning true.",
		          m_file.getFilename());
		return true;
	}

	if ( ! m_needToWrite ) {
		logTrace( g_conf.m_logTraceRdbIndex, "END. no need, not writing index. filename [%s]. Returning true.",
		          m_file.getFilename());
		return true;
	}

	// open a new file
	if ( ! m_file.open ( O_RDWR | O_CREAT | O_TRUNC ) ) {
		log(LOG_ERROR, "%s:%s: END. Could not open %s for writing: %s. Returning false.",
		    __FILE__, __func__, m_file.getFilename(), mstrerror(g_errno));
		return false;
	}
	   
			   
	// write index data
	bool status = writeIndex2();

	// on success, we don't need to write it anymore
	if ( status ) {
		m_needToWrite = false;
	}


	logTrace( g_conf.m_logTraceRdbIndex, "END. filename [%s], returning %s", m_file.getFilename(), status ? "true" : "false");

	return status;
}


bool RdbIndex::writeIndex2 ( ) {
	logTrace( g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	//@todo: IMPLEMENT!
	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);

	
	// the current disk offset
//	int64_t offset = 0LL;
	g_errno = 0;
	
#if 0	
	// first 8 bytes are the size of the DATA file we're indexing
	m_file.write ( &m_offset , 8 , offset );
	if ( g_errno )  {
		log(LOG_ERROR, "%s:%s: Failed to write to %s (m_offset): %s",
		    __FILE__, __func__, m_file.getFilename(), mstrerror(g_errno));
		return false;
	}
#endif

	logTrace( g_conf.m_logTraceRdbIndex, "END - OK, returning true." );

	return true;
}



bool RdbIndex::readIndex() 
{
	logTrace( g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());
	
	// bail if does not exist
	if ( ! m_file.doesExist() ) {
		log(LOG_ERROR,"%s:%s:%d: Index file [%s] does not exist.", __FILE__, __func__, __LINE__,  m_file.getFilename());
		logTrace( g_conf.m_logTraceRdbIndex, "END. Returning false" );
		return false;
	}

			   
	// . open the file
	// . do not open O_RDONLY because if we are resuming a killed merge
	//   we will add to this index and write it back out.
	if ( ! m_file.open ( O_RDWR ) ) {
		log(LOG_ERROR,"%s:%s:%d: Could not open index file %s for reading: %s.",__FILE__, __func__, __LINE__, m_file.getFilename(),mstrerror(g_errno));
		logTrace( g_conf.m_logTraceRdbIndex, "END. Returning false" );
		return false;
	}
			   
			   
	bool status = readIndex2();

	m_file.closeFds();
	
	logTrace( g_conf.m_logTraceRdbIndex, "END. Returning %s", status?"true":"false");
	// return status
	return status;
}


bool RdbIndex::readIndex2 ( ) {
	g_errno = 0;

	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);
	//@todo: IMPLEMENT!
	
#if 0
	// first 8 bytes are the size of the DATA file we're indexing
	m_file.read ( &m_offset , 8 , offset );
	if ( g_errno ) {
		log( LOG_WARN, "db: Had error reading %s: %s.", m_file.getFilename(),mstrerror(g_errno));
		return false;
	}
	offset += 8;
#endif
	return true;
}


bool RdbIndex::addRecord ( char rdbId, char *key ) {
	
	if( rdbId == RDB_POSDB ) {

		if( key[0]&0x02 || !(key[0]&0x04) ) {
			//it is a 12-byte docid+pos or 18-byte termid+docid+pos key

			uint64_t doc_id = extract_bits(key,58,96);
			if( doc_id != m_lastDocId ) {
				log(LOG_ERROR, "@@@ GOT DocId %" PRIu64 "", doc_id);

				m_lastDocId = doc_id;
				
				//@todo: IMPLEMENT!
				log( LOG_ERROR,"%s:%s: ADD TO INDEX - NOT IMPLEMENTED YET", __FILE__, __func__);
				
				
				m_needToWrite = true;
			}
		}
	}
	return true;
}



void RdbIndex::printIndex () {
	//@todo: IMPLEMENT!
	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);
}



// . attempts to auto-generate from data file, f
// . returns false and sets g_errno on error
bool RdbIndex::generateIndex ( BigFile *f ) {
	//@todo: IMPLEMENT!
	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);

	reset();
	if ( g_conf.m_readOnlyMode ) {
		return false;
	}

	log( LOG_ERROR, "%s:%s: Generating index for %s/%s - NOT IMPLEMENTED!",__FILE__, __func__, f->getDir(),f->getFilename());

	if ( ! m_file.open ( O_RDWR | O_CREAT | O_TRUNC ) ) {
		log( LOG_ERROR, "%s:%s: Generating index %s/%s - failed to create file",__FILE__, __func__, m_file.getDir(),m_file.getFilename());
		return false;
	}

	m_file.write ( (void*)"hello\n" , 6 , 0);

	m_file.closeFds();
		
	// fake that it went well
	return true;
}


