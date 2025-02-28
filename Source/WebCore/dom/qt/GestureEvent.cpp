/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "GestureEvent.h"

#if ENABLE(QT_GESTURE_EVENTS)

#include "Element.h"
#include <wtf/text/AtomString.h>

namespace WebCore {

RefPtr<GestureEvent> GestureEvent::create(AbstractView* view, const PlatformGestureEvent& event)
{
    AtomString eventType;
    switch (event.type()) {
    case PlatformEvent::GestureTap:
        eventType = eventNames().gesturetapEvent; break;
    case PlatformEvent::GestureLongPress:
    default:
        return 0;
    }
    return adoptRef(new GestureEvent(eventType, MonotonicTime(event.timestamp()), view, event.globalPosition().x(), event.globalPosition().y(), event.position().x(), event.position().y(), event.modifierKeys()));
}

EventInterface GestureEvent::eventInterface() const
{
    // FIXME: This makes it so we never wrap GestureEvents in the right bindings.
    return EventInterfaceType;
}

GestureEvent::GestureEvent(const AtomString& type, MonotonicTime timestamp, AbstractView* view, int screenX, int screenY, int clientX, int clientY, OptionSet<Modifier> modifiers)
    : MouseRelatedEvent(type, CanBubble::Yes, IsCancelable::Yes, IsComposed::Yes, timestamp, view, 0, IntPoint(screenX, screenY), IntPoint(clientX, clientY)
#if ENABLE(POINTER_LOCK)
        , IntPoint(0, 0)
#endif
        , modifierKeys)
{
}

} // namespace WebCore

#endif // ENABLE(QT_GESTURE_EVENTS)
