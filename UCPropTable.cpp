#include "gb-include.h"

#include "Mem.h"
#include "UCPropTable.h"

UCPropTable::UCPropTable(u_char valueSize, 
			 u_char tableBits) {
	m_valueSize = valueSize;
	m_tableBits = tableBits;
	m_numTables = 0xF0000 >> m_tableBits;
	m_tableSize = (1 << m_tableBits) * m_valueSize;
	m_tableMask = (1 << m_tableBits) - 1;
	m_data = NULL;
}

UCPropTable::~UCPropTable() {
	reset();
}

void UCPropTable::reset() {
	if (m_data) {
		for (u_int32_t i=0;i<m_numTables;i++) {
			if (m_data[i])
				mfree(m_data[i], m_tableSize , "UCPropTable");
		}
		mfree(m_data, m_numTables*sizeof(u_char**),
			"UCPropTable");
		m_data = NULL;
	}	
}

/*
void *UCPropTable::getValue(uint32_t c){
	uint32_t prefix = c >> m_tableBits;
	uint32_t key = c & m_tableMask;
	if (prefix >= m_numTables) return NULL;
	if (m_data[prefix] == NULL) return NULL;
	return (void*) (m_data[prefix] + key*m_valueSize);
}
*/

bool UCPropTable::setValue(u_int32_t c, const void* value) {
	u_int32_t prefix = c >> m_tableBits;
	uint16_t key = c & m_tableMask;
	if (prefix >= m_numTables) return false; // invalid plane
	if (m_data == NULL){
		m_data = (u_char**)
			mmalloc(m_numTables * sizeof(m_data[0]),
				"UCPropTable");
		if (m_data == NULL) {
			log(LOG_WARN, "UCPropTable: out of memory");
			return false;
		}
		memset(m_data, '\0', m_numTables*sizeof(m_data[0]));
	}
	if (m_data[prefix] == NULL){
		m_data[prefix] = (u_char*) 
			mmalloc(m_tableSize, "UCPropTable");
		if (m_data[prefix] == NULL) {
			log(LOG_WARN, "UCPropTable: out of memory");
			return false;
		}
		
		memset(m_data[prefix], '\0', m_tableSize);
	}
	gbmemcpy(m_data[prefix] +key*m_valueSize, value, m_valueSize);
	return true;
	
}

size_t UCPropTable::getStoredSize() const {
	// Header
	u_int32_t size = sizeof(u_int32_t) // record size
		+ sizeof(u_char) // value size
		+ sizeof(u_char);  // number of table bits

	if (m_data)
		for (u_int32_t i=0 ; i < m_numTables ; i++) {
			if (m_data[i]) {
				size += sizeof(int32_t) + // table #
					m_tableSize;
				
			}
		}
	size += sizeof (u_int32_t);
	return size;
}
#define RECORD_END (u_int32_t)0xdeadbeef

size_t UCPropTable::serialize(char *buf, size_t bufSize) const {
	uint32_t size = getStoredSize();
	if (bufSize < size) return 0;
	char *p = buf;
	// Header
	*(uint32_t*)p = size; p += sizeof(u_int32_t);
	*(u_char*)p = m_valueSize; p += sizeof(u_char);
	*(u_char*)p = m_tableBits; p += sizeof(u_char);
	if (m_data)
		for (u_int32_t i=0; i<m_numTables ; i++) {
			if (m_data[i]) {
				*(u_int32_t*)p = i; p += sizeof(u_int32_t);
				gbmemcpy(p, m_data[i], m_tableSize);
				p += m_tableSize;
			}
		}
	*(uint32_t*)p = RECORD_END; p += sizeof(u_int32_t);
	// sanity check
	if (p != buf + size) {
		log(LOG_WARN, "UCPropTable: size mismatch: expected %" PRId32" bytes, "
			"but wrote %" PRId32" instead", (int32_t) size, (int32_t) (p - buf));
		return false;
	}
	return p-buf;
}

size_t UCPropTable::deserialize(const void *buf, size_t bufSize) {
	reset();
	const char * const bufStart = (const char*)buf;
	const char * const bufEnd = bufStart+bufSize;
	const char *p = bufStart;
	if(bufSize < 4+1+1)
		return 0;
	u_int32_t size = *(u_int32_t*)p; p+=sizeof(u_int32_t);
	if(size > bufSize)
		return 0;

	m_valueSize = *(u_char*)p++;
	m_tableBits = *(u_char*)p++;
	//printf ("Read %d bytes after header\n", p-bufStart);
	m_tableSize = (1 << m_tableBits) * m_valueSize;
	m_tableMask = (1 << m_tableBits) - 1;

	m_numTables = 0xF0000 >> m_tableBits;
	// allocate main table
	m_data = (u_char**) mmalloc(m_numTables * sizeof(m_data[0]), "UCPropTable");
	if (m_data == NULL) {
		log(LOG_WARN, "UCPropTable: out of memory");
		return false;
	}
	memset(m_data, 0, m_numTables*sizeof(m_data[0]));
	
	//load tables
	while (p < bufEnd) {
		u_int32_t prefix = *(u_int32_t*)p; p += sizeof(u_int32_t);
		if ( prefix == RECORD_END ){
			if (p != bufEnd ) {
				log(LOG_WARN, "UCPropTable: unexpected end of record");
				return false;
			}
			//printf ("Read %d bytes after footer\n", p-bufStart);
			return size;

		}
		if(prefix<m_numTables) {
			m_data[prefix] = (u_char*) mmalloc(m_tableSize, "UCPropTable");
			if (m_data[prefix] == NULL) {
				log(LOG_WARN, "UCPropTable: out of memory");
				return false;
			}
			memcpy(m_data[prefix], p, m_tableSize); p += m_tableSize;
		} else
			log(LOG_WARN,"UCPropTable: got invalid prefix %u at offset %tu", prefix, p-bufStart);
	}
	// shouldn't get here
	log("UCPropTable: No RECORD_END found");
	return 0;
}
