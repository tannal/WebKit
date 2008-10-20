/*
    Copyright (C) 2004, 2005, 2006, 2007, 2008 Nikolas Zimmermann <zimmermann@kde.org>
                  2004, 2005, 2006, 2008 Rob Buis <buis@kde.org>
    Copyright (C) 2008 Apple Inc. All rights reserved.
    Copyright (C) 2008 Alp Toker <alp@atoker.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"

#if ENABLE(SVG)
#include "SVGElement.h"

#include "DOMImplementation.h"
#include "Document.h"
#include "Event.h"
#include "EventListener.h"
#include "EventNames.h"
#include "FrameView.h"
#include "HTMLNames.h"
#include "PlatformString.h"
#include "RenderObject.h"
#include "SVGDocumentExtensions.h"
#include "SVGElementInstance.h"
#include "SVGNames.h"
#include "SVGResource.h"
#include "SVGSVGElement.h"
#include "SVGURIReference.h"
#include "SVGUseElement.h"
#include "XMLNames.h"
#include "RegisteredEventListener.h"

namespace WebCore {

using namespace HTMLNames;
using namespace EventNames;

SVGElement::SVGElement(const QualifiedName& tagName, Document* doc)
    : StyledElement(tagName, doc)
    , m_shadowParent(0)
{
}

SVGElement::~SVGElement()
{
}

bool SVGElement::isSupported(StringImpl* feature, StringImpl* version) const
{
    return DOMImplementation::hasFeature(feature, version);
}

String SVGElement::id() const
{
    return getAttribute(idAttr);
}

void SVGElement::setId(const String& value, ExceptionCode&)
{
    setAttribute(idAttr, value);
}

String SVGElement::xmlbase() const
{
    return getAttribute(XMLNames::baseAttr);
}

void SVGElement::setXmlbase(const String& value, ExceptionCode&)
{
    setAttribute(XMLNames::baseAttr, value);
}

SVGSVGElement* SVGElement::ownerSVGElement() const
{
    Node* n = isShadowNode() ? const_cast<SVGElement*>(this)->shadowParentNode() : parentNode();
    while (n) {
        if (n->hasTagName(SVGNames::svgTag))
            return static_cast<SVGSVGElement*>(n);

        n = n->isShadowNode() ? n->shadowParentNode() : n->parentNode();
    }

    return 0;
}

SVGElement* SVGElement::viewportElement() const
{
    // This function needs shadow tree support - as RenderSVGContainer uses this function
    // to determine the "overflow" property. <use> on <symbol> wouldn't work otherwhise.
    Node* n = isShadowNode() ? const_cast<SVGElement*>(this)->shadowParentNode() : parentNode();
    while (n) {
        if (n->hasTagName(SVGNames::svgTag) || n->hasTagName(SVGNames::imageTag) || n->hasTagName(SVGNames::symbolTag))
            return static_cast<SVGElement*>(n);

        n = n->isShadowNode() ? n->shadowParentNode() : n->parentNode();
    }

    return 0;
}

SVGDocumentExtensions* SVGElement::accessDocumentSVGExtensions() const
{

    // This function is provided for use by SVGAnimatedProperty to avoid
    // global inclusion of Document.h in SVG code.
    return document() ? document()->accessSVGExtensions() : 0;
}
 
void SVGElement::mapInstanceToElement(SVGElementInstance* instance)
{
    ASSERT(instance);
    ASSERT(!m_elementInstances.contains(instance));
    m_elementInstances.add(instance);
}
 
void SVGElement::removeInstanceMapping(SVGElementInstance* instance)
{
    ASSERT(instance);
    ASSERT(m_elementInstances.contains(instance));
    m_elementInstances.remove(instance);
}

HashSet<SVGElementInstance*> SVGElement::instancesForElement() const
{
    return m_elementInstances;
}

void SVGElement::parseMappedAttribute(MappedAttribute* attr)
{
    // standard events
    if (attr->name() == onloadAttr)
        setEventListenerForTypeAndAttribute(loadEvent, attr);
    else if (attr->name() == onclickAttr)
        setEventListenerForTypeAndAttribute(clickEvent, attr);
    else if (attr->name() == onmousedownAttr)
        setEventListenerForTypeAndAttribute(mousedownEvent, attr);
    else if (attr->name() == onmousemoveAttr)
        setEventListenerForTypeAndAttribute(mousemoveEvent, attr);
    else if (attr->name() == onmouseoutAttr)
        setEventListenerForTypeAndAttribute(mouseoutEvent, attr);
    else if (attr->name() == onmouseoverAttr)
        setEventListenerForTypeAndAttribute(mouseoverEvent, attr);
    else if (attr->name() == onmouseupAttr)
        setEventListenerForTypeAndAttribute(mouseupEvent, attr);
    else if (attr->name() == SVGNames::onfocusinAttr)
        setEventListenerForTypeAndAttribute(DOMFocusInEvent, attr);
    else if (attr->name() == SVGNames::onfocusoutAttr)
        setEventListenerForTypeAndAttribute(DOMFocusOutEvent, attr);
    else if (attr->name() == SVGNames::onactivateAttr)
        setEventListenerForTypeAndAttribute(DOMActivateEvent, attr);
    else
        StyledElement::parseMappedAttribute(attr);
}

bool SVGElement::haveLoadedRequiredResources()
{
    Node* child = firstChild();
    while (child) {
        if (child->isSVGElement() && !static_cast<SVGElement*>(child)->haveLoadedRequiredResources())
            return false;
        child = child->nextSibling();
    }
    return true;
}

static bool hasLoadListener(SVGElement* node)
{
    Node* currentNode = node;
    while (currentNode && currentNode->isElementNode()) {
        RegisteredEventListenerList *list = static_cast<Element*>(currentNode)->localEventListeners();
        if (list) {
            RegisteredEventListenerList::Iterator end = list->end();
            for (RegisteredEventListenerList::Iterator it = list->begin(); it != end; ++it)
                if ((*it)->eventType() == loadEvent &&
                    (*it)->useCapture() == true || currentNode == node)
                    return true;
        }
        currentNode = currentNode->parentNode();
    }

    return false;
}

void SVGElement::sendSVGLoadEventIfPossible(bool sendParentLoadEvents)
{
    RefPtr<SVGElement> currentTarget = this;
    while (currentTarget && currentTarget->haveLoadedRequiredResources()) {
        RefPtr<Node> parent;
        if (sendParentLoadEvents)
            parent = currentTarget->parentNode(); // save the next parent to dispatch too incase dispatching the event changes the tree
        if (hasLoadListener(currentTarget.get())) {
            RefPtr<Event> event = Event::create(loadEvent, false, false);
            event->setTarget(currentTarget);
            ExceptionCode ignored = 0;
            currentTarget->dispatchGenericEvent(event.release(), ignored);
        }
        currentTarget = (parent && parent->isSVGElement()) ? static_pointer_cast<SVGElement>(parent) : 0;
    }
}

void SVGElement::finishParsingChildren()
{
    StyledElement::finishParsingChildren();

    // finishParsingChildren() is called when the close tag is reached for an element (e.g. </svg>)
    // we send SVGLoad events here if we can, otherwise they'll be sent when any required loads finish
    sendSVGLoadEventIfPossible();
}

bool SVGElement::childShouldCreateRenderer(Node* child) const
{
    if (child->isSVGElement())
        return static_cast<SVGElement*>(child)->isValid();
    return false;
}

void SVGElement::insertedIntoDocument()
{
    StyledElement::insertedIntoDocument();
    SVGDocumentExtensions* extensions = document()->accessSVGExtensions();

    String resourceId = SVGURIReference::getTarget(id());
    if (extensions->isPendingResource(resourceId)) {
        std::auto_ptr<HashSet<SVGStyledElement*> > clients(extensions->removePendingResource(resourceId));
        if (clients->isEmpty())
            return;

        HashSet<SVGStyledElement*>::const_iterator it = clients->begin();
        const HashSet<SVGStyledElement*>::const_iterator end = clients->end();

        for (; it != end; ++it)
            (*it)->buildPendingResource();

        SVGResource::invalidateClients(*clients);
    }
}

void SVGElement::attributeChanged(Attribute* attr, bool preserveDecls)
{
    ASSERT(attr);
    if (!attr)
        return;

    StyledElement::attributeChanged(attr, preserveDecls);
    svgAttributeChanged(attr->name());
}

void SVGElement::updateAnimatedSVGAttribute(const String& name) const
{
    ASSERT(!m_areSVGAttributesValid);

    if (m_synchronizingSVGAttributes)
        return;

    m_synchronizingSVGAttributes = true;

    if (name.isEmpty()) {
        invokeAllSVGPropertySynchronizers();
        setSynchronizedSVGAttributes(true);
    } else
        invokeSVGPropertySynchronizer(name);

    m_synchronizingSVGAttributes = false;
}

void SVGElement::setSynchronizedSVGAttributes(bool value) const
{
    m_areSVGAttributesValid = value;
}

}

#endif // ENABLE(SVG)
