/*
 * Copyright (C) 2011-2017 Apple Inc. All Rights Reserved.
 * Copyright (C) 2014 Raspberry Pi Foundation. All Rights Reserved.
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

#include <atomic>
#include <ctime>
#include <wtf/FastMalloc.h>
#include <wtf/Forward.h>
#include <wtf/Function.h>
#include <wtf/RunLoop.h>

#if OS(WINDOWS)
#include <wtf/win/Win32Handle.h>
#endif

#if PLATFORM(COCOA) || (PLATFORM(QT) && OS(DARWIN))
#include <wtf/OSObjectPtr.h>
#endif

namespace WTF {

enum class MemoryPressureStatus : uint8_t {
    Normal,

    // The entire system is at a warning or critical pressure level.
    SystemWarning,
    SystemCritical,

    // This specific process crossed a warning or critical memory usage limit.
    ProcessLimitWarning,
    ProcessLimitCritical
};

enum class MemoryUsagePolicy : uint8_t {
    Unrestricted, // Allocate as much as you want
    Conservative, // Maybe you don't cache every single thing
    Strict, // Time to start pinching pennies for real
};

enum class WebsamProcessState : uint8_t {
    Active,
    Inactive,
};

enum class Critical : bool { No, Yes };
enum class Synchronous : bool { No, Yes };

typedef WTF::Function<void(Critical, Synchronous)> LowMemoryHandler;

struct MemoryPressureHandlerConfiguration {
    WTF_MAKE_STRUCT_FAST_ALLOCATED;
    WTF_EXPORT_PRIVATE MemoryPressureHandlerConfiguration();
    WTF_EXPORT_PRIVATE MemoryPressureHandlerConfiguration(size_t, double, double, std::optional<double>, Seconds);

    size_t baseThreshold;
    double conservativeThresholdFraction;
    double strictThresholdFraction;
    std::optional<double> killThresholdFraction;
    Seconds pollInterval;
};

class MemoryPressureHandler {
    WTF_MAKE_FAST_ALLOCATED;
    friend class WTF::LazyNeverDestroyed<MemoryPressureHandler>;
public:
    WTF_EXPORT_PRIVATE static MemoryPressureHandler& singleton();

    WTF_EXPORT_PRIVATE void install();

    WTF_EXPORT_PRIVATE void setShouldUsePeriodicMemoryMonitor(bool);

#if OS(LINUX) || OS(FREEBSD)
    WTF_EXPORT_PRIVATE void triggerMemoryPressureEvent(bool isCritical);
#endif

    void setMemoryKillCallback(WTF::Function<void()>&& function) { m_memoryKillCallback = WTFMove(function); }
    void setMemoryPressureStatusChangedCallback(WTF::Function<void(MemoryPressureStatus)>&& function) { m_memoryPressureStatusChangedCallback = WTFMove(function); }

    void setLowMemoryHandler(LowMemoryHandler&& handler)
    {
        m_lowMemoryHandler = WTFMove(handler);
    }

    bool isUnderMemoryWarning() const
    {
        auto memoryPressureStatus = m_memoryPressureStatus.load();
        return memoryPressureStatus == MemoryPressureStatus::SystemWarning
            || memoryPressureStatus == MemoryPressureStatus::ProcessLimitWarning
#if PLATFORM(MAC)
            || m_memoryUsagePolicy == MemoryUsagePolicy::Conservative
#endif
            || m_isSimulatingMemoryWarning;
    }

    bool isUnderMemoryPressure() const
    {
        auto memoryPressureStatus = m_memoryPressureStatus.load();
        return memoryPressureStatus == MemoryPressureStatus::SystemCritical
            || memoryPressureStatus == MemoryPressureStatus::ProcessLimitCritical
#if PLATFORM(MAC)
            || m_memoryUsagePolicy >= MemoryUsagePolicy::Strict
#endif
            || m_isSimulatingMemoryPressure;
    }
    bool isSimulatingMemoryWarning() const { return m_isSimulatingMemoryWarning; }
    bool isSimulatingMemoryPressure() const { return m_isSimulatingMemoryPressure; }
    void setMemoryPressureStatus(MemoryPressureStatus);

    WTF_EXPORT_PRIVATE MemoryUsagePolicy currentMemoryUsagePolicy();

#if PLATFORM(COCOA) || (PLATFORM(QT) && OS(DARWIN))
    void setDispatchQueue(OSObjectPtr<dispatch_queue_t>&& queue)
    {
        RELEASE_ASSERT(!m_installed);
        m_dispatchQueue = WTFMove(queue);
    }
#endif

    class ReliefLogger {
        WTF_MAKE_FAST_ALLOCATED;
    public:
        explicit ReliefLogger(const char *log)
            : m_logString(log)
            , m_initialMemory(loggingEnabled() ? platformMemoryUsage() : MemoryUsage { })
        {
        }

        ~ReliefLogger()
        {
            if (loggingEnabled())
                logMemoryUsageChange();
        }


        const char* logString() const { return m_logString; }
        static void setLoggingEnabled(bool enabled) { s_loggingEnabled = enabled; }
        static bool loggingEnabled()
        {
#if RELEASE_LOG_DISABLED
            return s_loggingEnabled;
#else
            return true;
#endif
        }

    private:
        struct MemoryUsage {
            WTF_MAKE_STRUCT_FAST_ALLOCATED;
            MemoryUsage() = default;
            MemoryUsage(size_t resident, size_t physical)
                : resident(resident)
                , physical(physical)
            {
            }
            size_t resident { 0 };
            size_t physical { 0 };
        };
        std::optional<MemoryUsage> platformMemoryUsage();
        void logMemoryUsageChange();

        const char* m_logString;
        std::optional<MemoryUsage> m_initialMemory;

        WTF_EXPORT_PRIVATE static bool s_loggingEnabled;
    };

    using Configuration = MemoryPressureHandlerConfiguration;

    void setConfiguration(Configuration&& configuration) { m_configuration = WTFMove(configuration); }
    void setConfiguration(const Configuration& configuration) { m_configuration = configuration; }

    WTF_EXPORT_PRIVATE void releaseMemory(Critical, Synchronous = Synchronous::No);

    WTF_EXPORT_PRIVATE void beginSimulatedMemoryWarning();
    WTF_EXPORT_PRIVATE void endSimulatedMemoryWarning();
    WTF_EXPORT_PRIVATE void beginSimulatedMemoryPressure();
    WTF_EXPORT_PRIVATE void endSimulatedMemoryPressure();

    WTF_EXPORT_PRIVATE void setProcessState(WebsamProcessState);
    WebsamProcessState processState() const { return m_processState; }

    WTF_EXPORT_PRIVATE static ASCIILiteral processStateDescription();

    WTF_EXPORT_PRIVATE static void setPageCount(unsigned);

    void setShouldLogMemoryMemoryPressureEvents(bool shouldLog) { m_shouldLogMemoryMemoryPressureEvents = shouldLog; }

private:
    std::optional<size_t> thresholdForMemoryKill();
    size_t thresholdForPolicy(MemoryUsagePolicy);
    MemoryUsagePolicy policyForFootprint(size_t);

    void memoryPressureStatusChanged();

    void uninstall();

    void holdOff(Seconds);

    MemoryPressureHandler();
    ~MemoryPressureHandler() = delete;

    void respondToMemoryPressure(Critical, Synchronous = Synchronous::No);
    void platformReleaseMemory(Critical);
    void platformInitialize();

    void measurementTimerFired();
    void shrinkOrDie(size_t killThreshold);
    void setMemoryUsagePolicyBasedOnFootprint(size_t);

    unsigned m_pageCount { 0 };

    std::atomic<MemoryPressureStatus> m_memoryPressureStatus { MemoryPressureStatus::Normal };
    bool m_installed { false };
    bool m_isSimulatingMemoryWarning { false };
    bool m_isSimulatingMemoryPressure { false };
    bool m_shouldLogMemoryMemoryPressureEvents { true };

    WebsamProcessState m_processState { WebsamProcessState::Inactive };
    
    MemoryUsagePolicy m_memoryUsagePolicy { MemoryUsagePolicy::Unrestricted };

    std::unique_ptr<RunLoop::Timer>m_measurementTimer;
    WTF::Function<void()> m_memoryKillCallback;
    WTF::Function<void(MemoryPressureStatus)> m_memoryPressureStatusChangedCallback;
    LowMemoryHandler m_lowMemoryHandler;

    Configuration m_configuration;

#if OS(WINDOWS)
    void windowsMeasurementTimerFired();
    RunLoop::Timer m_windowsMeasurementTimer;
    Win32Handle m_lowMemoryHandle;
#endif

#if OS(LINUX) || OS(FREEBSD)
    RunLoop::Timer m_holdOffTimer;
    void holdOffTimerFired();
#endif

#if PLATFORM(COCOA) || (PLATFORM(QT) && OS(DARWIN))
    OSObjectPtr<dispatch_queue_t> m_dispatchQueue;
#endif
};

} // namespace WTF

using WTF::Critical;
using WTF::MemoryPressureHandler;
using WTF::Synchronous;
using WTF::WebsamProcessState;
