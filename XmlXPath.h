#ifndef GB_XMLXPATH_H
#define GB_XMLXPATH_H

#include <stdint.h>
#include "XmlNode.h"

class Xml;

class XmlXPath {
public:
	XmlXPath(const char *xpath);
	~XmlXPath();

	void print(int level = 0);
	bool getContent(Xml* xml, char *buf, int32_t maxLength , int32_t *contentLenPtr);

private:
	XmlXPath();

	bool parse(const char *xpath);

	bool m_valid;

	const char *m_xpath;

	bool m_hasNode;
	nodeid_t m_nodeId;

	bool m_hasPredicate;

	bool m_hasAttribute;
	const char *m_attribute;
	int32_t m_attributeLen;

	bool m_hasAttributeValue;
	char m_attributeValueQuote;
	const char *m_attributeValue;
	int32_t m_attributeValueLen;

	XmlXPath *m_child;
};

#endif // GB_XMLXPATH_H
