#include "DeltaApply.hpp"

#include <stdio.h>

#include "xercesc/dom/DOMNamedNodeMap.hpp"
#include "xercesc/dom/DOMNodeList.hpp"
#include "xercesc/dom/DOMImplementation.hpp"
#include "xercesc/dom/DOMImplementationRegistry.hpp"
#include "xercesc/dom/DOMAttr.hpp"
#include "xercesc/util/XMLString.hpp"
#include "xercesc/util/XMLUniDefs.hpp"

#include "DeltaSortOperations.hpp"
#include "Tools.hpp"

#include "xydiff/DeltaException.hpp"
#include "xydiff/XyLatinStr.hpp"
#include "xydiff/XyInt.hpp"
#include "xydiff/XyDelta_DOMInterface.hpp"
#include "xydiff/XyStrDelta.hpp"
#include "xydiff/XyDiffNS.hpp"

XERCES_CPP_NAMESPACE_USE

static const XMLCh gLS[] = { chLatin_L, chLatin_S, chNull };

XID_DOMDocument* DeltaApplyEngine::getResultDocument (void) {
	// if (applyAnnotations) {
	// 	XMLCh *xyDeltaNS_ch = XMLString::transcode("urn:schemas-xydiff:xydelta");
	// 	deltaDoc     = XID_DOMDocument::createDocument() ;
	// 	XMLCh *xmlnsURI_ch = XMLString::transcode("http://www.w3.org/2000/xmlns/");
	// 	XMLCh *xmlns_ch = XMLString::transcode("xmlns:xy");
	// 	DOMNode* resultRoot = xiddoc->getDocumentElement();
	// 	if (!((DOMElement*)resultRoot)->hasAttributeNS(xmlnsURI_ch, xmlns_ch)) {
	// 		((DOMElement*)resultRoot)->setAttributeNS(xmlnsURI_ch, xmlns_ch, xyDeltaNS_ch);
	// 	}
	// 	// ((DOMElement*)xiddoc)->setAttributeNS(xmlnsURI_ch, xmlns_ch, xyDeltaNS_ch);
	// 	XMLString::release(&xyDeltaNS_ch);
	// 	XMLString::release(&xmlnsURI_ch);
	// 	XMLString::release(&xmlns_ch);
	// }
  return xiddoc ;
}


/********************************************************************
 |*                                                                 *
 |* DeltaApplyEngine:: Atomic Operation implemented here            *
 |*                                                                 *
 ********************************************************************/

/* ---- MOVE FROM : Takes away a subtree in a document ---- */

void DeltaApplyEngine::Subtree_MoveFrom( XID_t myXID ) {
	vddprintf(("        move (from) node xid=%d\n", (int) myXID ));
	DOMNode* moveNode = xiddoc->getXidMap().getNodeWithXID( myXID );
	if (moveNode==NULL) THROW_AWAY(("node with XID=%d not found",(int)myXID));
	
	DOMNode* backupNode = moveDocument->importNode( moveNode, true );
	moveDocument->getXidMap().mapSubtree( xiddoc->getXidMap().String(moveNode).c_str() , backupNode );
	moveDocument->getDocumentElement()->appendChild( backupNode );
	
	moveNode = moveNode->getParentNode()->removeChild( moveNode );
	xiddoc->getXidMap().removeSubtree( moveNode );
	}

/* ---- DELETE a map of certain nodes in a document ---- */

void DeltaApplyEngine::Subtree_Delete( const char *xidmapStr ) {
	XidMap_Parser parse(xidmapStr) ;
	// Note here that the PostFix order for XID-map is important to garantuee that nodes will be deleted in the	correct order
	while (!parse.isListEmpty()) {
		XID_t deleteXID       = parse.getNextXID();
		vddprintf(( "        delete node xid=%d\n", (int) deleteXID ));
		DOMNode* deleteNode   = xiddoc->getXidMap().getNodeWithXID( deleteXID );
		if (deleteNode==NULL) THROW_AWAY(("node with XID=%d not found",(int)deleteXID));
		xiddoc->getXidMap().removeNode( deleteNode );
		deleteNode            = deleteNode->getParentNode()->removeChild( deleteNode );
		}
	}

/* ---- MOVE TO: puts a subtree in the document ---- */

void DeltaApplyEngine::Subtree_MoveTo( XID_t myXID, XID_t parentXID, int position ) {
	vddprintf(("        move subtree rooted by %d to (parent=%d, pos=%d)\n", (int)myXID, (int)parentXID, position));
	DOMNode* moveRoot = NULL;
	try {
		moveRoot = moveDocument->getXidMap().getNodeWithXID( myXID ) ;		
	} catch (...) {
		return;
	}
	if (moveRoot==NULL) return; //THROW_AWAY(("node with XID=%d not found",(int)myXID));
	
	Subtree_Insert(moveRoot, parentXID, position, moveDocument->getXidMap().String(moveRoot).c_str() );
	
	(void)moveRoot->getParentNode()->removeChild(moveRoot);
	}

/* ---- INSERT a map of certain nodes in a document ---- */

void DeltaApplyEngine::Subtree_Insert( DOMNode *insertSubtreeRoot, XID_t parentXID, int position, const char *xidmapStr ) {

	vddprintf(( "        insert xidmap=%s at (parent=%d, pos=%d)\n", xidmapStr, (int)parentXID, position));
	DOMNode* contentNode  = xiddoc->importNode( insertSubtreeRoot, true );
	DOMNode* parentNode   = xiddoc->getXidMap().getNodeWithXID( parentXID );
	if (parentNode==NULL) THROW_AWAY(("parent node with XID=%d not found",(int)parentXID));

	int actual_pos = 1 ;
	if ((position!=1)&&(!parentNode->hasChildNodes())) THROW_AWAY(("parent has no children but position is %d",position));
	DOMNode* brother = parentNode->getFirstChild();
	while (actual_pos < position) {
	  brother = brother->getNextSibling();
		actual_pos++;
		if ((brother==NULL)&&(actual_pos<position)) THROW_AWAY(("parent has %d children but position is %d",actual_pos-1, position));
		}
	
	// Add node to the tree
	if (brother==NULL) parentNode->appendChild( contentNode );
	else parentNode->insertBefore( contentNode, brother );
	
	xiddoc->getXidMap().mapSubtree( xidmapStr, contentNode );

	}

/* ---- UPDATE the text value of a given node ---- */

void DeltaApplyEngine::TextNode_Update( XID_t nodeXID, DOMNode *operationNode ) {
	vddprintf(("        update xid=%d\n",(int)nodeXID));
	DOMNode* upNode = xiddoc->getXidMap().getNodeWithXID( nodeXID );
	if (upNode==NULL) THROW_AWAY(("node with XID=%d not found",(int)nodeXID));
	
	DOMNodeList *opNodes = operationNode->getChildNodes();
	vddprintf(("opNodes->length() = %d\n", opNodes->getLength()));
	XyStrDeltaApply *xytext = new XyStrDeltaApply(xiddoc, upNode, 1);
	xytext->setApplyAnnotations(applyAnnotations);
	for (int i = opNodes->getLength() - 1; i >= 0; i--) {
		DOMElement *op = (DOMElement *) opNodes->item(i);
		char *optype = XMLString::transcode(op->getLocalName());
		XMLCh pos_attr[4];
		XMLCh len_attr[4];
		XMLString::transcode("pos", pos_attr, 3);
		XMLString::transcode("len", len_attr, 3);
		vddprintf(("item %d = %s\n", i, optype));
		// Replace operation
		if (strcmp(optype, "tr") == 0) {
			char *pos = XMLString::transcode(op->getAttribute(pos_attr));
			char *len = XMLString::transcode(op->getAttribute(len_attr));
			xytext->replace(atoi(pos), atoi(len), op->getTextContent());
			XMLString::release(&pos);
			XMLString::release(&len);
		}
		// Delete operation
		else if (strcmp(optype, "td") == 0) {
			char *pos = XMLString::transcode(op->getAttribute(pos_attr));
			char *len = XMLString::transcode(op->getAttribute(len_attr));
			xytext->remove(atoi(pos), atoi(len));
			XMLString::release(&pos);
			XMLString::release(&len);
		}
		// Insert operation
		else if (strcmp(optype, "ti") == 0) {
			char *pos = XMLString::transcode(op->getAttribute(pos_attr));
			xytext->insert(atoi(pos), op->getTextContent());
			XMLString::release(&pos);
		}
		XMLString::release(&optype);
	}
	xytext->complete();
	delete xytext;
}



/* ---- ATTRIBUTE Operations ---- */

void DeltaApplyEngine::Attribute_Insert( XID_t nodeXID, const XMLCh* attr, const XMLCh* value ) {
	vddprintf(("        insert attr at xid=%d\n",(int)nodeXID));
	DOMNode* node = xiddoc->getXidMap().getNodeWithXID( nodeXID );
	if (node==NULL) THROW_AWAY(("node with XID=%d not found",(int)nodeXID));
	DOMAttr* attrNode = xiddoc->createAttribute( attr );
	attrNode->setNodeValue( value );
	DOMNamedNodeMap *attrs = node->getAttributes();
	if (attrs == NULL) {
		if (node->getNodeType() != DOMNode::ELEMENT_NODE) {
			THROW_AWAY(("Attempted to insert an attribute on a non-element node. Perhaps xidmap was corrupted?"));
		} else {
			THROW_AWAY(("Unexpected error encountered: getAttributes() returned NULL for element"));
		}
	} else {
		attrs->setNamedItem( attrNode );
	}
	
}
	
void DeltaApplyEngine::Attribute_Update( XID_t nodeXID, const XMLCh* attr, const XMLCh* value ) {
	vddprintf(("        update attr at xid=%d\n",(int)nodeXID));
	DOMNode* node = xiddoc->getXidMap().getNodeWithXID( nodeXID );
	if (node==NULL) THROW_AWAY(("node with XID=%d not found",(int)nodeXID));
	node->getAttributes()->getNamedItem( attr )->setNodeValue( value );
	}
	
void DeltaApplyEngine::Attribute_Delete( XID_t nodeXID, const XMLCh* attr ) {
	vddprintf(("        delete attr at xid=%d\n",(int)nodeXID));
	DOMNode* node = xiddoc->getXidMap().getNodeWithXID( nodeXID );
	if (node==NULL) THROW_AWAY(("node with XID=%d not found",(int)nodeXID));
	node = node->getAttributes()->removeNamedItem( attr );
	}
	
/********************************************************************
 |*                                                                 *
 |* DeltaApplyEngine:: ApplyOperation                               *
 |*                                                                 *
 ********************************************************************/
	
void DeltaApplyEngine::ApplyOperation(DOMNode *operationNode) {
	 
	vddprintf(("ApplyOperation\n"));
	XMLCh dStr[2];
	XMLCh iStr[2];
	XMLCh uStr[2];
	XMLCh adStr[3];
	XMLCh aiStr[3];
	XMLCh auStr[3];
	XMLCh renameRootStr[11];
	XMLString::transcode("d", dStr, 1);
	XMLString::transcode("i", iStr, 1);
	XMLString::transcode("u", uStr, 1);
	XMLString::transcode("ad", adStr, 2);
	XMLString::transcode("ai", aiStr, 2);
	XMLString::transcode("au", auStr, 2);
	XMLString::transcode("renameRoot", renameRootStr, 10);
	XMLCh tempStr[6];
	if (XMLString::equals(operationNode->getLocalName(), dStr)) {
		vddprintf(("        d(elete)\n"));
		
		bool move = false ;
		XMLString::transcode("move", tempStr, 5);
		DOMNode* moveAttr = operationNode->getAttributes()->getNamedItem(tempStr) ;
		XMLString::transcode("yes", tempStr, 5);
		if ((moveAttr!=NULL) && (XMLString::equals(moveAttr->getNodeValue(),tempStr))) {
			move = true;
		}

		XMLString::transcode("xm", tempStr, 5);
		char *xidmapStr = XMLString::transcode(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());		
		if (move) {
			XidMap_Parser parse(xidmapStr) ;
			XID_t myXid = parse.getRootXID();
			Subtree_MoveFrom( myXid );
		  }
		else {
			Subtree_Delete(xidmapStr) ;
		}
		XMLString::release(&xidmapStr);
	}

	else if (XMLString::equals(operationNode->getLocalName(),iStr)) {
		vddprintf(("        i(nsert)\n"));

		bool move = false ;
		XMLString::transcode("move", tempStr, 5);
		DOMNode* moveAttr = operationNode->getAttributes()->getNamedItem(tempStr) ;
		XMLString::transcode("yes", tempStr, 5);
		if ( (moveAttr!=NULL) && (XMLString::equals( moveAttr->getNodeValue(), tempStr ))) {
			move = true;
		}
		XMLString::transcode("pos", tempStr, 5);
		DOMNode *n = operationNode->getAttributes()->getNamedItem(tempStr);
		int position = XyInt(n->getNodeValue());

		XMLString::transcode("par", tempStr, 5);
		n = operationNode->getAttributes()->getNamedItem(tempStr);
		XID_t parentXID = (XID_t)(int)XyInt(n->getNodeValue());

		XMLString::transcode("xm", tempStr, 5);
		char *xidmapStr = XMLString::transcode(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());
		if (move) {
			XidMap_Parser parse(xidmapStr) ;
			XID_t myXid = parse.getRootXID();

			Subtree_MoveTo( myXid, parentXID, position );
		}
		else {
			DOMNode* insertRoot ;
			// get data to insert
			if (operationNode->hasChildNodes()) insertRoot = operationNode->getFirstChild() ;
			else THROW_AWAY(("insert operator element contains no data"));
				
			Subtree_Insert( insertRoot, parentXID, position, xidmapStr ) ;
		}
		XMLString::release(&xidmapStr);
	}

	else if (XMLString::equals(operationNode->getLocalName(), uStr)) {
		vddprintf(("        u(pdate)\n"));
		XMLString::transcode("oldxm", tempStr, 5);
		char *xidmapStr = XMLString::transcode(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());
		XidMap_Parser parse(xidmapStr) ;
		XID_t nodeXID = parse.getRootXID();
		TextNode_Update( nodeXID, operationNode);
		XMLString::release(&xidmapStr);
		}

	else if (XMLString::equals(operationNode->getLocalName(), adStr)) {
		vddprintf(("        a(ttribute) d(elete)\n"));
		XMLString::transcode("xid", tempStr, 5);
		XID_t nodeXID = (XID_t)(int)XyInt(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());

		XMLString::transcode("a", tempStr, 5);
        const XMLCh* attr = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		Attribute_Delete( nodeXID, attr );
		}
	else if (XMLString::equals(operationNode->getLocalName(), aiStr)) {
		vddprintf(("        a(ttribute) i(nsert)\n"));
		XMLString::transcode("xid", tempStr, 5);
		XID_t nodeXID = (XID_t)(int)XyInt(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());
		XMLString::transcode("a", tempStr, 5);
        const XMLCh* attr = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		XMLString::transcode("v", tempStr, 5);
        const XMLCh* value = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		Attribute_Insert( nodeXID, attr, value );
		}
	else if (XMLString::equals(operationNode->getLocalName(), auStr)) {
		vddprintf(("        a(ttribute) u(pdate)\n"));
		XMLString::transcode("xid", tempStr, 5);
		XID_t nodeXID = (XID_t)(int)XyInt(operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue());
		XMLString::transcode("a", tempStr, 5);
        const XMLCh* attr = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		XMLString::transcode("nv", tempStr, 5);
        const XMLCh* value = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		Attribute_Update( nodeXID, attr, value );
		}
	else if (XMLString::equals(operationNode->getLocalName(), renameRootStr)) {
		vddprintf(("        renameRoot\n"));
		DOMNode *root = xiddoc->getDocumentElement();
		XID_t rootXID = xiddoc->getXidMap().getXIDbyNode(root);
		XMLString::transcode("to", tempStr, 5);
        const XMLCh* newrootName = operationNode->getAttributes()->getNamedItem(tempStr)->getNodeValue() ;
		DOMElement* newroot = xiddoc->createElement(newrootName);

		DOMNode* child = root->getFirstChild();
		while(child!=NULL) {
			root->removeChild(child);
			newroot->appendChild(child);
			child = root->getFirstChild();
		}
		DOMNamedNodeMap *attributes = root->getAttributes();
		for(unsigned int i=0;i<attributes->getLength();i++) {
			DOMNode *an = attributes->item(i);
			newroot->setAttribute(an->getNodeName(), an->getNodeValue());
		}
		xiddoc->removeChild(root);
		xiddoc->getXidMap().removeNode(root);
		root->release();
		xiddoc->appendChild(newroot);
		xiddoc->getXidMap().registerNode(newroot, rootXID);
		xiddoc->getXidMap().SetRootElement(newroot);
		}
	}

/********************************************************************
 |*                                                                 *
 |*                                                                 *
 |* DeltaApply                                                      *
 |*                                                                 *
 |*                                                                 *
 ********************************************************************/

void DeltaApplyEngine::ApplyDeltaElement(DOMNode* incDeltaElement) {
	vddprintf(("Apply delta element\n"));
	deltaElement = incDeltaElement;
	
	/* ---- Do All DELETE Operations ( including 'from' part of move ) ---- */
	
	vddprintf(("Apply Delete operations\n"));
	DOMNode* firstOp = deltaElement->getFirstChild() ;
	vddprintf(("    first sort delete operations...\n"));
	SortDeleteOperationsEngine deleteOps(xiddoc, firstOp);
	vddprintf(("    then apply all of them 1 by 1...\n"));
	while(!deleteOps.isListEmpty()) {
		DOMNode* op=deleteOps.getNextDeleteOperation();
		ApplyOperation(op);
		}

	vddprintf(("Ok, there are no more delete operations.\n"));
	
	/* ---- Do All INSERT Operations ( including 'to' part of move ) ---- */
	
	firstOp = deltaElement->getFirstChild() ;
	SortInsertOperationsEngine insertOps(xiddoc, firstOp);
	while(!insertOps.isListEmpty()) {
		DOMNode* op=insertOps.getNextInsertOperation();
		ApplyOperation(op);
		}
	
	/* ---- Do all  UPDATE  &  ATTRIBUTE  Operations ---- */

	DOMNode* child = deltaElement->getFirstChild() ;
	XMLCh iStr[100];
	XMLString::transcode("i", iStr, 99);
	XMLCh dStr[100];
	XMLString::transcode("d", dStr, 99);
	while (child != NULL) {
		if ( (!XMLString::equals(child->getLocalName(),iStr))
		
		   &&(!XMLString::equals(child->getLocalName(), dStr)) ) ApplyOperation(child);
	  child = child->getNextSibling() ;
		}
		
	/* ---- Small consistency checks ---- */

	if (moveDocument->getDocumentElement()->hasChildNodes()) THROW_AWAY(("temporary document used to move node is not empty!"));

	vddprintf(("xiddoc=%s\n",xiddoc->getXidMap().String().c_str() ));
}

void DeltaApplyEngine::ApplyDelta(XID_DOMDocument *IncDeltaDoc) {
	
	deltaElement      = DeltaApplyEngine::getDeltaElement(IncDeltaDoc);
	deltaDoc          = IncDeltaDoc ;
	
	// vddprintf(("FROM: %s\n", DeltaApplyEngine::getSourceURI(IncDeltaDoc).c_str() ));
	// vddprintf(("TO:   %s\n", DeltaApplyEngine::getDestinationURI(IncDeltaDoc).c_str() ));

	ApplyDeltaElement(deltaElement);
}



// for applying more delta elements (backwardNumber) one time

void DeltaApplyEngine::ApplyDelta(XID_DOMDocument *IncDeltaDoc, int backwardNumber) {
    
	DOMNode* deltaRoot = IncDeltaDoc->getDocumentElement();  // <delta_unit>
	DOMNode* deltaElement ;
		for ( deltaElement = deltaRoot->getLastChild(); backwardNumber > 0;  deltaElement = deltaElement->getPreviousSibling(), backwardNumber-- ) {
                  DOMImplementation* impl =  DOMImplementationRegistry::getDOMImplementation(gLS);
                  DOMDocument* backwardDeltaDoc = impl->createDocument(0, XMLString::transcode(""),0);
                
                  //DOMDocument* backwardDeltaDoc = DOMDocument::createDocument();
                  DOMNode* backwardDeltaElement = XyDelta::ReverseDelta(backwardDeltaDoc, deltaElement);			
			ApplyDeltaElement(backwardDeltaElement);
		}
	
}

DeltaApplyEngine::DeltaApplyEngine(XID_DOMDocument *sourceDoc) {
  
	if (sourceDoc    == NULL)          THROW_AWAY(("document is null"));
	xiddoc            = sourceDoc ;

	vddprintf(("creating temporary move document\n"));
	moveDocument = XID_DOMDocument::createDocument() ;
	moveDocument->initEmptyXidmapStartingAt(-1);
	XMLCh *moveRootCh = XMLString::transcode("moveRoot");
	DOMElement* moveRoot = moveDocument->createElement(moveRootCh) ;
	XMLString::release(&moveRootCh);
	moveDocument->appendChild( moveRoot );
	applyAnnotations = false;
}

DeltaApplyEngine::~DeltaApplyEngine() {
	moveDocument->release();
	delete moveDocument;
}
DOMNode* DeltaApplyEngine::getDeltaElement(XID_DOMDocument *IncDeltaDoc) {

	if (IncDeltaDoc  == NULL) THROW_AWAY(("delta document is null"));

	DOMNode* dRoot = IncDeltaDoc->getElementsByTagNameNS(XYDIFF_XYDELTA_NS, XMLString::transcode("unit_delta"))->item(0);
	if ( (dRoot==NULL)
	   ||(!XMLString::equals(dRoot->getLocalName(), XMLString::transcode("unit_delta")))) THROW_AWAY(("no <unit_delta> root found on document"));

//	const XMLCh * firstChildNodeName = dRoot->getFirstChild()->getNodeName();
//	char * firstChildNodeChar = XMLString::transcode(firstChildNodeName);
	DOMNodeList * tElementNodes = IncDeltaDoc->getElementsByTagNameNS(XYDIFF_XYDELTA_NS, XMLString::transcode("t"));
	if (tElementNodes->getLength() == 0) THROW_AWAY(("no delta element <t> found"));
//	   ||(!XMLString::equals(dRoot->getFirstChild()->getNodeName(), tempStr))) THROW_AWAY(("no delta element <t> found"));
	
//	DOMNode* deltaElement = dRoot->getFirstChild() ;
	DOMNode* deltaElement = tElementNodes->item(0);
	return deltaElement ;
	}


std::string DeltaApplyEngine::getSourceURI(XID_DOMDocument *IncDeltaDoc) {

	DOMNode* deltaElement = DeltaApplyEngine::getDeltaElement(IncDeltaDoc);
	DOMNode* fromItem = deltaElement->getAttributes()->getNamedItem(XMLString::transcode("from"));
	if (fromItem==NULL) THROW_AWAY(("attribute 'from' not found"));
	
        return std::string(XyLatinStr(fromItem->getNodeValue()));
	}
	
std::string DeltaApplyEngine::getDestinationURI(XID_DOMDocument *IncDeltaDoc) {
	DOMNode* deltaElement = DeltaApplyEngine::getDeltaElement(IncDeltaDoc);
	DOMNode* toItem = deltaElement->getAttributes()->getNamedItem(XMLString::transcode("to"));
	if (toItem==NULL) THROW_AWAY(("attribute 'to' not found"));
	
        return std::string(XyLatinStr(toItem->getNodeValue()));
	}

void DeltaApplyEngine::setApplyAnnotations(bool paramApplyAnnotations)
{
	applyAnnotations = paramApplyAnnotations;
}

bool DeltaApplyEngine::getApplyAnnotations()
{
	return applyAnnotations;
}