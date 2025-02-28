/*
 * Copyright (C) 2020-2023 Apple Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NativeImage.h"

namespace WebCore {

#if !USE(CG)
RefPtr<NativeImage> NativeImage::create(PlatformImagePtr&& platformImage, RenderingResourceIdentifier renderingResourceIdentifier)
{
#if PLATFORM(QT)
    if (platformImage.isNull())
#else
    if (!platformImage)
#endif
        return nullptr;

    return adoptRef(*new NativeImage(WTFMove(platformImage), renderingResourceIdentifier));
}

RefPtr<NativeImage> NativeImage::createTransient(PlatformImagePtr&& image, RenderingResourceIdentifier identifier)
{
    return create(WTFMove(image), identifier);
}
#endif

NativeImage::NativeImage(PlatformImagePtr&& platformImage, RenderingResourceIdentifier renderingResourceIdentifier)
    : RenderingResource(renderingResourceIdentifier)
#if PLATFORM(QT)
    , m_platformImage(platformImage)
#else
    , m_platformImage(WTFMove(platformImage))
#endif
{
#if PLATFORM(QT)
    ASSERT(!m_platformImage.isNull());
#else
    ASSERT(m_platformImage);
#endif
}
    
void NativeImage::setPlatformImage(PlatformImagePtr&& platformImage)
{
#if PLATFORM(QT)
    m_platformImage = QImage(platformImage);
#else
    ASSERT(platformImage);
    m_platformImage = WTFMove(platformImage);
#endif
}

} // namespace WebCore
