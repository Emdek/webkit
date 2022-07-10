/*
 * Copyright (C) 2010, 2015 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc.  All rights reserved.
 * Copyright (C) 2017 Sony Interactive Entertainment Inc.
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

#pragma once

#include <wtf/Forward.h>
#include <wtf/FunctionDispatcher.h>
#include <wtf/Seconds.h>
#include <wtf/ThreadSafetyAnalysis.h>
#include <wtf/Threading.h>

#if USE(COCOA_EVENT_LOOP) || (PLATFORM(QT) && USE(MACH_PORTS))
#include <dispatch/dispatch.h>
#include <wtf/OSObjectPtr.h>
#elif PLATFORM(QT) && USE(UNIX_DOMAIN_SOCKETS)
#include <QSocketNotifier>
#else
#include <wtf/RunLoop.h>
#endif

#if PLATFORM(QT) && USE(UNIX_DOMAIN_SOCKETS)
QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE
#endif

namespace WTF {

class WorkQueueBase : public FunctionDispatcher, public ThreadSafeRefCounted<WorkQueueBase> {
public:
    using QOS = Thread::QOS;

    ~WorkQueueBase() override;

    WTF_EXPORT_PRIVATE void dispatch(Function<void()>&&) override;
    WTF_EXPORT_PRIVATE void dispatchWithQOS(Function<void()>&&, QOS);
    WTF_EXPORT_PRIVATE virtual void dispatchAfter(Seconds, Function<void()>&&);
    WTF_EXPORT_PRIVATE virtual void dispatchSync(Function<void()>&&);

#if USE(COCOA_EVENT_LOOP)
    dispatch_queue_t dispatchQueue() const { return m_dispatchQueue.get(); }
#elif PLATFORM(QT) && USE(UNIX_DOMAIN_SOCKETS)
    QSocketNotifier* registerSocketEventHandler(int, QSocketNotifier::Type, WTF::Function<void()>&&);
    void dispatchOnTermination(QProcess*, WTF::Function<void()>&&);
#endif

protected:
    enum class Type : bool {
        Serial,
        Concurrent
    };
    WorkQueueBase(const char* name, Type, QOS);
#if USE(COCOA_EVENT_LOOP) || (PLATFORM(QT) && USE(MACH_PORTS))
    explicit WorkQueueBase(OSObjectPtr<dispatch_queue_t>&&);
#elif !PLATFORM(QT)
    explicit WorkQueueBase(RunLoop&);
#endif

    void platformInitialize(const char* name, Type, QOS);
    void platformInvalidate();

#if USE(COCOA_EVENT_LOOP) || (PLATFORM(QT) && USE(MACH_PORTS))
    OSObjectPtr<dispatch_queue_t> m_dispatchQueue;
#elif PLATFORM(QT) && USE(UNIX_DOMAIN_SOCKETS)
    class WorkItemQt;
    QThread* m_workThread;
    friend class WorkItemQt;
#else
    RunLoop* m_runLoop;
#if ASSERT_ENABLED
    uint32_t m_threadID { 0 };
#endif
#endif
};

/**
 * A WorkQueue is a function dispatching interface like FunctionDispatcher.
 * Runnables dispatched to a WorkQueue are required to execute serially.
 * That is, two different runnables dispatched to the WorkQueue should never be allowed to execute simultaneously.
 * They may be executed on different threads but can safely be used by objects that aren't already threadsafe.
 * Use `assertIsCurrent(m_myQueue);` in a runnable to assert that the runnable runs in a specific queue.
 */
class WTF_CAPABILITY("is current") WorkQueue : public WorkQueueBase {
public:
    WTF_EXPORT_PRIVATE static WorkQueue& main();

    WTF_EXPORT_PRIVATE static Ref<WorkQueue> create(const char* name, QOS = QOS::Default);

#if PLATFORM(QT) && USE(UNIX_DOMAIN_SOCKETS)
    class WorkItemQt;
    QThread* m_workThread;
    friend class WorkItemQt;
#elif !USE(COCOA_EVENT_LOOP) && !(PLATFORM(QT) && USE(MACH_PORTS))
    RunLoop& runLoop() const { return *m_runLoop; }
#endif

protected:
    WorkQueue(const char* name, QOS qos)
        : WorkQueueBase(name, Type::Serial, qos)
    {
    }
private:
#if USE(COCOA_EVENT_LOOP) || (PLATFORM(QT) && USE(MACH_PORTS))
    explicit WorkQueue(OSObjectPtr<dispatch_queue_t>&&);
#elif !PLATFORM(QT)
    explicit WorkQueue(RunLoop&);
#endif
    static Ref<WorkQueue> constructMainWorkQueue();

#if ASSERT_ENABLED
    WTF_EXPORT_PRIVATE void assertIsCurrent() const;
    friend void assertIsCurrent(const WorkQueue&);
#endif
};

inline void assertIsCurrent(const WorkQueue& workQueue) WTF_ASSERTS_ACQUIRED_CAPABILITY(workQueue)
{
#if ASSERT_ENABLED
    workQueue.assertIsCurrent();
#else
    UNUSED_PARAM(workQueue);
#endif
}

/**
 * A ConcurrentWorkQueue unlike a WorkQueue doesn't guarantee the order in which the dispatched runnable will run
 * and each can run concurrently on different threads.
 */
class ConcurrentWorkQueue final : public WorkQueueBase {
public:
    WTF_EXPORT_PRIVATE static Ref<ConcurrentWorkQueue> create(const char* name, QOS = QOS::Default);
    WTF_EXPORT_PRIVATE static void apply(size_t iterations, WTF::Function<void(size_t index)>&&);
private:
    ConcurrentWorkQueue(const char* name, QOS qos)
        : WorkQueueBase(name, Type::Concurrent, qos)
    {
    }
};

}

using WTF::WorkQueue;
using WTF::ConcurrentWorkQueue;
using WTF::assertIsCurrent;
