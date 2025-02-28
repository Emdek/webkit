/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "WKContentWorldInternal.h"

#import "_WKUserContentWorldInternal.h"
#import <WebCore/WebCoreObjCExtras.h>

@implementation WKContentWorld

+ (WKContentWorld *)pageWorld
{
    return wrapper(API::ContentWorld::pageContentWorld());
}

+ (WKContentWorld *)defaultClientWorld
{
    return wrapper(API::ContentWorld::defaultClientWorld());
}

+ (WKContentWorld *)worldWithName:(NSString *)name
{
    return wrapper(API::ContentWorld::sharedWorldWithName(name)).autorelease();
}

- (void)dealloc
{
    if (WebCoreObjCScheduleDeallocateOnMainRunLoop(WKContentWorld.class, self))
        return;

    _contentWorld->~ContentWorld();

    [super dealloc];
}

- (NSString *)name
{
    if (_contentWorld->name().isNull())
        return nil;

    return _contentWorld->name();
}

#pragma mark WKObject protocol implementation

- (API::Object&)_apiObject
{
    return *_contentWorld;
}

@end

@implementation WKContentWorld (WKPrivate)

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
- (_WKUserContentWorld *)_userContentWorld
{
    return adoptNS([[_WKUserContentWorld alloc] _initWithContentWorld:self]).autorelease();
}
ALLOW_DEPRECATED_DECLARATIONS_END

@end
