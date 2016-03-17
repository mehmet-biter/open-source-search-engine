#include "XmlXPath.h"
#include "Mem.h"
#include "Log.h"
#include "Unicode.h"
#include "Xml.h"
#include "HttpMime.h"
#include "Words.h"
#include "Pos.h"

XmlXPath::XmlXPath( const char *xpath )
    : m_valid(false)
    , m_xpath(xpath)
    , m_hasNode(false)
    , m_nodeId(0)
    , m_hasPredicate(false)
    , m_hasAttribute(false)
    , m_attribute(NULL)
    , m_attributeLen(0)
    , m_hasAttributeValue(false)
    , m_attributeValueQuote('\0')
    , m_attributeValue(NULL)
    , m_attributeValueLen(0)
    , m_child(NULL) {
	m_valid = parse( xpath );
}

XmlXPath::XmlXPath()
    : m_valid(false)
    , m_xpath(NULL)
    , m_hasNode(false)
    , m_nodeId(0)
    , m_hasPredicate(false)
    , m_hasAttribute(false)
    , m_attribute(NULL)
    , m_attributeLen(0)
    , m_hasAttributeValue(false)
    , m_attributeValueQuote('\0')
    , m_attributeValue(NULL)
    , m_attributeValueLen(0)
    , m_child(NULL) {
}

XmlXPath::~XmlXPath() {
	if ( m_child ) {
		mdelete ( m_child , sizeof(*m_child) , "xpathitem" );
		delete (m_child);
	}
}

void XmlXPath::print( int level ) {
	if ( m_hasNode ) {
		log(LOG_INFO, "%*s hasNode node=%d", level, " ", m_nodeId);
	}

	if ( m_hasPredicate ) {
		log(LOG_INFO, "%*s hasPredicate ", level, " ");
	}

	if ( m_hasAttribute ) {
		log( LOG_INFO, "%*s hasAttribute value='%.*s' len=%d", level, " ", m_attributeLen, m_attribute,
		      m_attributeLen );
	}

	if ( m_hasAttributeValue ) {
		log( LOG_INFO, "%*s hasAttributeValue value='%.*s' len=%d", level, " ", m_attributeValueLen,
		      m_attributeValue, m_attributeValueLen );
	}

	if ( m_child ) {
		m_child->print( ++level );
	}
}

static void logXPathError ( const char *reason, const char *xpath, size_t currentPos = 0 ) {
	if (currentPos > 0) {
		log( LOG_WARN, "xml: %s for xpath='%s' at '%s' with pos=%ld",
		     reason, xpath, xpath + currentPos, currentPos );
	} else {
		log( LOG_WARN, "xml: %s for xpath='%s'",
		     reason, xpath);
	}
}

bool XmlXPath::parse( const char *xpath ) {
	if ( xpath == NULL) {
		return false;
	}

	size_t xpathLen = strlen(xpath);
	if ( xpathLen == 0 ) {
		return false;
	}

	size_t currentPos = 0;

	XmlXPath *xmlXPath = this;
	for ( ; currentPos < xpathLen; ++currentPos ) {
		char c = xpath[currentPos];

		if ( is_alpha_a( c ) ) {
			if ( !xmlXPath->m_hasNode && !xmlXPath->m_hasAttribute ) {
				NodeType *nodeType = NULL;
				nodeid_t nodeId = getTagId( xpath + currentPos, &nodeType );
				if ( nodeId == TAG_TEXTNODE ) {
					// invalid tag id
					logXPathError( "invalid tag id", xpath, currentPos );
					return false;
				}

				// found node
				xmlXPath->m_hasNode = true;
				xmlXPath->m_nodeId = nodeId;
				currentPos += ( strlen( nodeType->m_nodeName ) - 1 );
			} else {
				// @todo what do we do here?
			}
		} else if ( c == '[' ) {
			if (xmlXPath->m_hasPredicate) {
				// we can't support multiple predicate
				logXPathError("multiple predicate found", xpath, currentPos);
				return false;
			} else {
				xmlXPath->m_hasPredicate = true;
			}
		} else if ( c == ']' ) {
			// closing of predicate
		} else if ( c == '@' ) {
			// attribute
			if (xmlXPath->m_hasAttribute) {
				// we can't support multiple attribute
				logXPathError("multiple attribute found", xpath, currentPos);
				return false;
			} else {
				++currentPos;

				xmlXPath->m_hasAttribute = true;
				xmlXPath->m_attribute = xpath + currentPos;
			}
		} else if ( c == '=' ) {
			// validate
			if ( xmlXPath->m_hasAttributeValue ) {
				// we can't support multiple attribute value
				logXPathError("multiple attribute value found", xpath, currentPos);
				return false;
			} else if ( xmlXPath->m_hasPredicate && xmlXPath->m_hasAttribute ) {
				xmlXPath->m_attributeLen = currentPos - (xmlXPath->m_attribute - xpath);

				++currentPos;

				c = xpath[currentPos];
				if ( c == '\'' || c == '"' ) {
					++currentPos;
					xmlXPath->m_hasAttributeValue = true;
					xmlXPath->m_attributeValueQuote = c;
					xmlXPath->m_attributeValue = xpath + currentPos;
				} else {
					// no quotes found
					logXPathError("attribute value without quote", xpath, currentPos);
					return false;
				}
			} else {
				// we should be in predicate & attribute to be able to support '=' sign
				logXPathError( "unable to support '=' sign", xpath, currentPos );
				return false;
			}
		} else if ( c == '\'' || c == '"' ) {
			if ( xmlXPath->m_hasAttributeValue ) {
				if ( !xmlXPath->m_attributeValueLen ) {
					xmlXPath->m_attributeValueLen = currentPos - (xmlXPath->m_attributeValue - xpath);
				} else {
					logXPathError("quote found with attribute value len set", xpath, currentPos);
					return false;
				}
			} else {
				// quotes found without attribute value
				logXPathError("quote found without attribute value", xpath, currentPos);
				return false;
			}
		} else if ( c == '/' ) {
			if ( !xmlXPath->m_hasPredicate && xmlXPath->m_hasAttribute ) {
				logXPathError("no predicate but has attribute and found child", xpath, currentPos);
				return false;
			}

			if ( xmlXPath->m_hasAttribute && !xmlXPath->m_attributeLen ) {
				if ( !xmlXPath->m_hasAttributeValue ) {
					xmlXPath->m_attributeLen = currentPos - (xmlXPath->m_attribute - xpath);
				} else {
					logXPathError( "attribute does not have length when attribute value is available", xpath );
					return false;
				}
			}

			static const char *textFunc = "text()";
			static const size_t textFuncLen = strlen(textFunc);

			// check if it's text()
			if ( ( currentPos + 1 + textFuncLen == xpathLen ) &&
			     ( memcmp( xpath + currentPos + 1, textFunc, textFuncLen ) == 0 ) ) {
				// we're done
				break;
			}

			xmlXPath->m_valid = true;
			xmlXPath->m_child = new XmlXPath();
			mnew( xmlXPath->m_child, sizeof( *xmlXPath->m_child ), "xpathitem" );
			xmlXPath = xmlXPath->m_child;
		}
	}

	if (xmlXPath->m_hasAttribute && !xmlXPath->m_attributeLen) {
		xmlXPath->m_attributeLen = currentPos - (xmlXPath->m_attribute - xpath);
	}

	xmlXPath->m_valid = true;

	return true;
}

bool XmlXPath::getContent(Xml* xml, char *buf, int32_t maxLength , int32_t *contentLenPtr) {
	if ( !m_valid ) {
		return false;
	}

	int32_t nodeStart = 0;
	int32_t nodeEnd = xml->getNumNodes();

	XmlXPath *xmlXPath = this;
	for (int32_t currentNode = nodeStart; currentNode < nodeEnd; ++currentNode) {
		for (;;) {
			if ( xmlXPath->m_hasNode ) {
				if ( xml->getNodeId( currentNode ) != xmlXPath->m_nodeId ) {
					break;
				}
			}

			Words words;

			if (xmlXPath->m_hasAttribute) {
				if (xmlXPath->m_hasPredicate) {
					if (xmlXPath->m_hasAttributeValue) {
						int32_t valueLen = 0;
						char *value = xml->getNodePtr(currentNode)->getAttrValue(xmlXPath->m_attribute,
						                                                         xmlXPath->m_attributeLen, &valueLen);

						if ( xmlXPath->m_attributeValueLen != valueLen ||
						     memcmp( xmlXPath->m_attributeValue, value, valueLen ) ) {
							// doesn't match
							break;
						}
					} else {
						/// @todo not implemented: has predicate but no attribute value
						log(LOG_WARN, "xml: Not implemented. xpath='%s'", m_xpath);
						return false;
					}
				} else {
					int32_t valueLen = 0;
					char *value = xml->getNodePtr(currentNode)->getAttrValue(xmlXPath->m_attribute,
					                                                         xmlXPath->m_attributeLen, &valueLen);
					if ( !value || valueLen <= 0 ) {
						// no content
						break;
					}

					Xml innerXml;
					{
						/// @todo ALC workaround until we fix xml to use len instead of '\0'
						char saved = value[valueLen];
						value[valueLen] = '\0';

						if ( !innerXml.set( value, valueLen, xml->getVersion(), 0, CT_HTML ) ) {
							value[valueLen] = saved;
							return false;
						}

						value[valueLen] = saved;
					}

					if ( ( !words.set( &innerXml, true ) ) ) {
						// unable to allocate buffer
						return false;
					}
				}
			}

			if ( xmlXPath->m_child ) {
				xmlXPath = xmlXPath->m_child;
				continue;
			}

			if ( words.getContent() == NULL ) {
				int32_t currentEndNode = xml->getEndNode(currentNode);

				if (currentEndNode > currentNode) {
					if ( !words.set( xml, true, 0, currentNode, currentEndNode ) ) {
						// unable to allocate buffer
						return false;
					}
				} else {
					// unable to find end node
					break;
				}
			}

			Pos pos;
			if ( !pos.set( &words ) ) {
				// unable to allocate buffer
				return false;
			}

			int32_t contentLen = pos.filter( &words, 0, words.getNumWords(), true, buf, buf + maxLength, xml->getVersion() );
			if ( contentLen > 0 ) {
				if (contentLenPtr) {
					*contentLenPtr = contentLen;
				}

				return true;
			}

			break;
		}
	}

	return false;
}
