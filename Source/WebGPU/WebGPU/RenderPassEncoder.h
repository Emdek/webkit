/*
 * Copyright (c) 2021-2023 Apple Inc. All rights reserved.
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

#pragma once

#import "CommandsMixin.h"
#import <wtf/FastMalloc.h>
#import <wtf/HashMap.h>
#import <wtf/HashSet.h>
#import <wtf/HashTraits.h>
#import <wtf/Ref.h>
#import <wtf/RefCounted.h>
#import <wtf/Vector.h>

@class TextureAndClearColor;

struct WGPURenderPassEncoderImpl {
};

namespace WebGPU {

class BindGroup;
class Buffer;
class CommandEncoder;
class Device;
class QuerySet;
class RenderBundle;
class RenderPipeline;

// https://gpuweb.github.io/gpuweb/#gpurenderpassencoder
class RenderPassEncoder : public WGPURenderPassEncoderImpl, public RefCounted<RenderPassEncoder>, public CommandsMixin {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static Ref<RenderPassEncoder> create(id<MTLRenderCommandEncoder> renderCommandEncoder, const WGPURenderPassDescriptor& descriptor, NSUInteger visibilityResultBufferSize, bool depthReadOnly, bool stencilReadOnly, CommandEncoder& parentEncoder, id<MTLBuffer> visibilityResultBuffer, Device& device)
    {
        return adoptRef(*new RenderPassEncoder(renderCommandEncoder, descriptor, visibilityResultBufferSize, depthReadOnly, stencilReadOnly, parentEncoder, visibilityResultBuffer, device));
    }
    static Ref<RenderPassEncoder> createInvalid(Device& device)
    {
        return adoptRef(*new RenderPassEncoder(device));
    }

    ~RenderPassEncoder();

    void beginOcclusionQuery(uint32_t queryIndex);
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance);
    void drawIndexedIndirect(const Buffer& indirectBuffer, uint64_t indirectOffset);
    void drawIndirect(const Buffer& indirectBuffer, uint64_t indirectOffset);
    void endOcclusionQuery();
    void endPass();
    void executeBundles(Vector<std::reference_wrapper<RenderBundle>>&& bundles);
    void insertDebugMarker(String&& markerLabel);
    void popDebugGroup();
    void pushDebugGroup(String&& groupLabel);
    void setBindGroup(uint32_t groupIndex, const BindGroup&, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets);
    void setBlendConstant(const WGPUColor&);
    void setIndexBuffer(const Buffer&, WGPUIndexFormat, uint64_t offset, uint64_t size);
    void setPipeline(const RenderPipeline&);
    void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void setStencilReference(uint32_t);
    void setVertexBuffer(uint32_t slot, const Buffer&, uint64_t offset, uint64_t size);
    void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth);
    void setLabel(String&&);

    Device& device() const { return m_device; }

    bool isValid() const { return m_renderCommandEncoder; }

private:
    RenderPassEncoder(id<MTLRenderCommandEncoder>, const WGPURenderPassDescriptor&, NSUInteger, bool depthReadOnly, bool stencilReadOnly, CommandEncoder&, id<MTLBuffer>, Device&);
    RenderPassEncoder(Device&);

    bool validatePopDebugGroup() const;

    void makeInvalid();
    void executePreDrawCommands();

    id<MTLRenderCommandEncoder> m_renderCommandEncoder { nil };

    uint64_t m_debugGroupStackSize { 0 };
    struct PendingTimestampWrites {
        Ref<QuerySet> querySet;
        uint32_t queryIndex;
    };
    Vector<PendingTimestampWrites> m_pendingTimestampWrites;

    const Ref<Device> m_device;
    MTLPrimitiveType m_primitiveType { MTLPrimitiveTypeTriangle };
    id<MTLBuffer> m_indexBuffer { nil };
    MTLIndexType m_indexType { MTLIndexTypeUInt16 };
    NSUInteger m_indexBufferOffset { 0 };
    NSUInteger m_visibilityResultBufferOffset { 0 };
    NSUInteger m_visibilityResultBufferSize { 0 };
    bool m_depthReadOnly { false };
    bool m_stencilReadOnly { false };
    Vector<uint32_t> m_vertexDynamicOffsets;
    Vector<uint32_t> m_fragmentDynamicOffsets;
    const RenderPipeline* m_pipeline { nullptr };
    RefPtr<CommandEncoder> m_parentEncoder;
    HashMap<uint32_t, Vector<uint32_t>, DefaultHash<uint32_t>, WTF::UnsignedWithZeroKeyHashTraits<uint32_t>> m_bindGroupDynamicOffsets;
    float m_minDepth { 0.f };
    float m_maxDepth { 1.f };
    HashSet<uint64_t, DefaultHash<uint32_t>, WTF::UnsignedWithZeroKeyHashTraits<uint32_t>> m_queryBufferIndicesToClear;
    id<MTLBuffer> m_visibilityResultBuffer { nil };
    uint32_t m_renderTargetWidth { 0 };
    uint32_t m_renderTargetHeight { 0 };
    NSMutableDictionary<NSNumber*, TextureAndClearColor*> *m_attachmentsToClear { nil };
    NSMutableDictionary<NSNumber*, TextureAndClearColor*> *m_allColorAttachments { nil };
    id<MTLTexture> m_depthStencilAttachmentToClear { nil };
    float m_depthClearValue { 0 };
    uint32_t m_stencilClearValue { 0 };
    bool m_clearDepthAttachment { false };
    bool m_clearStencilAttachment { false };
    bool m_issuedDrawCall { false };
};

} // namespace WebGPU
