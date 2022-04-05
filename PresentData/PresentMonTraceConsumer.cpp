// Copyright (C) 2017-2022 Intel Corporation
// SPDX-License-Identifier: MIT

#include "PresentMonTraceConsumer.hpp"

#include "ETW/Microsoft_Windows_D3D9.h"
#include "ETW/Microsoft_Windows_Dwm_Core.h"
#include "ETW/Microsoft_Windows_DXGI.h"
#include "ETW/Microsoft_Windows_DxgKrnl.h"
#include "ETW/Microsoft_Windows_EventMetadata.h"
#include "ETW/Microsoft_Windows_Win32k.h"

#include <algorithm>
#include <assert.h>
#include <d3d9.h>
#include <dxgi.h>
#include <unordered_set>

#ifdef DEBUG
static constexpr int PRESENTEVENT_CIRCULAR_BUFFER_SIZE = 32768;
#else
static constexpr int PRESENTEVENT_CIRCULAR_BUFFER_SIZE = 8192;
#endif

namespace {

// Detect if there are any missing expected events, and returns the number of
// PresentStop events that we should wait for them.
uint32_t GetDeferredCompletionWaitCount(PresentEvent const& p)
{
    // If the present was displayed or discarded before Present_Stop, defer
    // completion for for one Present_Stop.
    if (p.Runtime != Runtime::Other && p.TimeTaken == 0) {
        return 1;
    }

    // All expected events already observed
    return 0;
}

}

// These macros, when enabled, record what PresentMon analysis below was done
// for each present.  The primary use case is to compute usage statistics and
// ensure test coverage.
//
// Add a TRACK_PRESENT_PATH() calls to every location that represents a unique
// analysis path.  e.g., as a starting point this might be one for every ETW
// event used below, with further instrumentation when there is different
// handling based on event property values.
//
// If the location is in a function that can be called by multiple parents, use
// TRACK_PRESENT_SAVE_PATH_ID() instead and call
// TRACK_PRESENT_GENERATE_PATH_ID() in each parent.
#ifdef TRACK_PRESENT_PATHS
#define TRACK_PRESENT_PATH_SAVE_ID(present, id) present->AnalysisPath |= 1ull << (id % 64)
#define TRACK_PRESENT_PATH(present) do { \
    enum { TRACK_PRESENT_PATH_ID = __COUNTER__ }; \
    TRACK_PRESENT_PATH_SAVE_ID(present, TRACK_PRESENT_PATH_ID); \
} while (0)
#define TRACK_PRESENT_PATH_GENERATE_ID()              mAnalysisPathID = __COUNTER__
#define TRACK_PRESENT_PATH_SAVE_GENERATED_ID(present) TRACK_PRESENT_PATH_SAVE_ID(present, mAnalysisPathID)
#else
#define TRACK_PRESENT_PATH(present)                   (void) present
#define TRACK_PRESENT_PATH_GENERATE_ID()
#define TRACK_PRESENT_PATH_SAVE_GENERATED_ID(present) (void) present
#endif

PresentEvent::PresentEvent(EVENT_HEADER const& hdr, ::Runtime runtime)
    : QpcTime(*(uint64_t*) &hdr.TimeStamp)
    , ProcessId(hdr.ProcessId)
    , ThreadId(hdr.ThreadId)
    , TimeTaken(0)
    , ReadyTime(0)
    , ScreenTime(0)

    , SwapChainAddress(0)
    , SyncInterval(-1)
    , PresentFlags(0)

    , CompositionSurfaceLuid(0)
    , Win32KPresentCount(0)
    , Win32KBindId(0)
    , DxgkPresentHistoryToken(0)
    , DxgkPresentHistoryTokenData(0)
    , DxgkContext(0)
    , Hwnd(0)
    , mAllPresentsTrackingIndex(UINT32_MAX)
    , QueueSubmitSequence(0)

    , DestWidth(0)
    , DestHeight(0)
    , DriverBatchThreadId(0)

    , Runtime(runtime)
    , PresentMode(PresentMode::Unknown)
    , FinalState(PresentResult::Unknown)

    , SupportsTearing(false)
    , MMIO(false)
    , SeenDxgkPresent(false)
    , SeenWin32KEvents(false)
    , DwmNotified(false)
    , SeenInFrameEvent(false)
    , CompletionIsDeferred(false)
    , IsCompleted(false)
    , IsLost(false)
    , PresentInDwmWaitingStruct(false)
{
#ifdef TRACK_PRESENT_PATHS
    AnalysisPath = 0ull;
#endif

#if DEBUG_VERBOSE
    static uint64_t presentCount = 0;
    presentCount += 1;
    Id = presentCount;
#endif
}

PMTraceConsumer::PMTraceConsumer()
    : mAllPresents(PRESENTEVENT_CIRCULAR_BUFFER_SIZE)
{
}

void PMTraceConsumer::HandleD3D9Event(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto const& hdr = pEventRecord->EventHeader;

    if (!IsProcessTrackedForFiltering(hdr.ProcessId)) {
        return;
    }

    switch (hdr.EventDescriptor.Id) {
    case Microsoft_Windows_D3D9::Present_Start::Id:
    {
        EventDataDesc desc[] = {
            { L"pSwapchain" },
            { L"Flags" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto pSwapchain = desc[0].GetData<uint64_t>();
        auto Flags      = desc[1].GetData<uint32_t>();

        auto present = std::make_shared<PresentEvent>(hdr, Runtime::D3D9);
        present->SwapChainAddress = pSwapchain;
        present->PresentFlags =
            ((Flags & D3DPRESENT_DONOTFLIP) ? DXGI_PRESENT_DO_NOT_SEQUENCE : 0) |
            ((Flags & D3DPRESENT_DONOTWAIT) ? DXGI_PRESENT_DO_NOT_WAIT : 0) |
            ((Flags & D3DPRESENT_FLIPRESTART) ? DXGI_PRESENT_RESTART : 0);
        if ((Flags & D3DPRESENT_FORCEIMMEDIATE) != 0) {
            present->SyncInterval = 0;
        }

        TrackPresentOnThread(present);
        TRACK_PRESENT_PATH(present);
        break;
    }
    case Microsoft_Windows_D3D9::Present_Stop::Id:
    {
        auto result = mMetadata.GetEventData<uint32_t>(pEventRecord, L"Result");

        bool AllowBatching =
            SUCCEEDED(result) &&
            result != S_PRESENT_OCCLUDED;

        RuntimePresentStop(hdr, AllowBatching, Runtime::D3D9);
        break;
    }
    default:
        assert(!mFilteredEvents); // Assert that filtering is working if expected
        break;
    }
}

void PMTraceConsumer::HandleDXGIEvent(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto const& hdr = pEventRecord->EventHeader;

    if (!IsProcessTrackedForFiltering(hdr.ProcessId)) {
        return;
    }

    switch (hdr.EventDescriptor.Id) {
    case Microsoft_Windows_DXGI::Present_Start::Id:
    case Microsoft_Windows_DXGI::PresentMultiplaneOverlay_Start::Id:
    {
        EventDataDesc desc[] = {
            { L"pIDXGISwapChain" },
            { L"Flags" },
            { L"SyncInterval" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto pIDXGISwapChain = desc[0].GetData<uint64_t>();
        auto Flags           = desc[1].GetData<uint32_t>();
        auto SyncInterval    = desc[2].GetData<int32_t>();

        // Ignore PRESENT_TEST: it's just to check if you're still fullscreen
        if ((Flags & DXGI_PRESENT_TEST) != 0) {
            // mPresentByThreadId isn't cleaned up properly when non-runtime
            // presents (e.g. created by Dxgk via FindOrCreatePresent())
            // complete.  So we need to clear mPresentByThreadId here to
            // prevent the corresponding Present_Stop event from modifying
            // anything.
            mPresentByThreadId.erase(hdr.ThreadId);
            break;
        }

        auto present = std::make_shared<PresentEvent>(hdr, Runtime::DXGI);
        present->SwapChainAddress = pIDXGISwapChain;
        present->PresentFlags     = Flags;
        present->SyncInterval     = SyncInterval;

        TrackPresentOnThread(present);
        TRACK_PRESENT_PATH(present);
        break;
    }
    case Microsoft_Windows_DXGI::Present_Stop::Id:
    case Microsoft_Windows_DXGI::PresentMultiplaneOverlay_Stop::Id:
    {
        auto result = mMetadata.GetEventData<uint32_t>(pEventRecord, L"Result");

        bool AllowBatching =
            SUCCEEDED(result) &&
            result != DXGI_STATUS_OCCLUDED &&
            result != DXGI_STATUS_MODE_CHANGE_IN_PROGRESS &&
            result != DXGI_STATUS_NO_DESKTOP_ACCESS;

        RuntimePresentStop(hdr, AllowBatching, Runtime::DXGI);
        break;
    }
    default:
        assert(!mFilteredEvents); // Assert that filtering is working if expected
        break;
    }
}

void PMTraceConsumer::HandleDxgkBlt(EVENT_HEADER const& hdr, uint64_t hwnd, bool redirectedPresent)
{
    // Lookup the in-progress present.
    auto presentEvent = FindOrCreatePresent(hdr);
    if (presentEvent == nullptr) {
        return;
    }

    // The looked-up present should not have a known present mode yet.  If it
    // does, we looked up a present whose tracking was lost for some reason.
    if (presentEvent->PresentMode != PresentMode::Unknown) {
        RemoveLostPresent(presentEvent);
        presentEvent = FindOrCreatePresent(hdr);
        if (presentEvent == nullptr) {
            return;
        }
        assert(presentEvent->PresentMode == PresentMode::Unknown);
    }

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(presentEvent);

    // This could be one of several types of presents. Further events will clarify.
    // For now, assume that this is a blt straight into a surface which is already on-screen.
    presentEvent->Hwnd = hwnd;
    if (redirectedPresent) {
        TRACK_PRESENT_PATH(presentEvent);
        presentEvent->PresentMode = PresentMode::Composed_Copy_CPU_GDI;
        presentEvent->SupportsTearing = false;
    } else {
        presentEvent->PresentMode = PresentMode::Hardware_Legacy_Copy_To_Front_Buffer;
        presentEvent->SupportsTearing = true;
    }
}

void PMTraceConsumer::HandleDxgkBltCancel(EVENT_HEADER const& hdr)
{
    // There are cases where a present blt can be optimized out in kernel.
    // In such cases, we return success to the caller, but issue no further work
    // for the present. Mark these cases as discarded.
    auto eventIter = mPresentByThreadId.find(hdr.ThreadId);
    if (eventIter != mPresentByThreadId.end()) {
        auto present = eventIter->second;

        TRACK_PRESENT_PATH(present);
        DebugModifyPresent(present.get());
        present->FinalState = PresentResult::Discarded;

        CompletePresent(present);
    }
}

void PMTraceConsumer::HandleDxgkFlip(EVENT_HEADER const& hdr, int32_t flipInterval, bool mmio)
{
    // A flip event is emitted during fullscreen present submission.
    // Afterwards, expect an MMIOFlip packet on the same thread, used to trace
    // the flip to screen.

    // Lookup the in-progress present.
    auto presentEvent = FindOrCreatePresent(hdr);
    if (presentEvent == nullptr) {
        return;
    }

    // The only events that we expect before a Flip/FlipMPO are a runtime
    // present start, or a previous FlipMPO.  If that's not the case, we assume
    // that correct tracking has been lost.
    while (presentEvent->QueueSubmitSequence != 0 || presentEvent->SeenDxgkPresent) {
        RemoveLostPresent(presentEvent);
        presentEvent = FindOrCreatePresent(hdr);
        if (presentEvent == nullptr) {
            return;
        }
    }

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(presentEvent);

    // For MPO, N events may be issued, but we only care about the first
    if (presentEvent->PresentMode != PresentMode::Unknown) {
        return;
    }

    presentEvent->MMIO = mmio;
    presentEvent->PresentMode = PresentMode::Hardware_Legacy_Flip;

    if (presentEvent->SyncInterval == -1) {
        presentEvent->SyncInterval = flipInterval;
    }
    if (!mmio) {
        presentEvent->SupportsTearing = flipInterval == 0;
    }

    // If this is the DWM thread, piggyback these pending presents on our fullscreen present
    if (hdr.ThreadId == DwmPresentThreadId) {
        for (auto iter = mPresentsWaitingForDWM.begin(); iter != mPresentsWaitingForDWM.end(); iter++) {
            iter->get()->PresentInDwmWaitingStruct = false;
        }
        std::swap(presentEvent->DependentPresents, mPresentsWaitingForDWM);
        DwmPresentThreadId = 0;
    }
}

template<bool Win7>
void PMTraceConsumer::HandleDxgkQueueSubmit(
    EVENT_HEADER const& hdr,
    uint32_t packetType,
    uint32_t submitSequence,
    uint64_t context,
    bool isPresentPacket)
{
    // For blt presents on Win7, the only way to distinguish between DWM-off
    // fullscreen blts and the DWM-on blt to redirection bitmaps is to track
    // whether a PHT was submitted before submitting anything else to the same
    // context, which indicates it's a redirected blt.  If we get to here
    // instead, the present is a fullscreen blt and considered completed once
    // its work is done.
    #pragma warning(suppress: 4984) // C++17 language extension
    if constexpr (Win7) {
        bool completedPresent = false;
        auto eventIter = mPresentByDxgkContext.find(context);
        if (eventIter != mPresentByDxgkContext.end()) {
            auto present = eventIter->second;

            TRACK_PRESENT_PATH(present);

            if (present->PresentMode == PresentMode::Hardware_Legacy_Copy_To_Front_Buffer) {
                DebugModifyPresent(present.get());
                present->SeenDxgkPresent = true;

                // If the work is already done, complete it now.
                if (present->ScreenTime != 0) {
                    CompletePresent(present);
                    completedPresent = true;
                }
            }

            // We're done with DxgkContext tracking, if the present hasn't
            // completed remove it from the tracking now.
            if (!completedPresent) {
                mPresentByDxgkContext.erase(eventIter);
            }
        }
    }

    // This event is emitted after a flip/blt/PHT event, and may be the only
    // way to trace completion of the present.
    if (packetType == (uint32_t) Microsoft_Windows_DxgKrnl::QueuePacketType::DXGKETW_MMIOFLIP_COMMAND_BUFFER ||
        packetType == (uint32_t) Microsoft_Windows_DxgKrnl::QueuePacketType::DXGKETW_SOFTWARE_COMMAND_BUFFER ||
        isPresentPacket) {
        auto eventIter = mPresentByThreadId.find(hdr.ThreadId);
        if (eventIter != mPresentByThreadId.end()) {
            auto present = eventIter->second;

            if (present->QueueSubmitSequence == 0) {

                TRACK_PRESENT_PATH(present);
                DebugModifyPresent(*present);

                present->QueueSubmitSequence = submitSequence;
                mPresentBySubmitSequence.emplace(submitSequence, present);

                #pragma warning(suppress: 4984) // C++17 language extension
                if constexpr (Win7) {
                    if (present->PresentMode == PresentMode::Hardware_Legacy_Copy_To_Front_Buffer) {
                        mPresentByDxgkContext[context] = present;
                        present->DxgkContext = context;
                    }
                }
            }
        }
    }
}

void PMTraceConsumer::HandleDxgkQueueComplete(EVENT_HEADER const& hdr, uint32_t submitSequence)
{
    // Check if this is a present Packet being tracked...
    auto eventIter = mPresentBySubmitSequence.find(submitSequence);
    if (eventIter == mPresentBySubmitSequence.end()) {
        return;
    }
    auto pEvent = eventIter->second;

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(pEvent);

    // If this is one of the present modes for which packet completion implies
    // display, then complete the present now.
    if (pEvent->PresentMode == PresentMode::Hardware_Legacy_Copy_To_Front_Buffer ||
        (pEvent->PresentMode == PresentMode::Hardware_Legacy_Flip && !pEvent->MMIO)) {
        DebugModifyPresent(*pEvent);
        pEvent->ReadyTime = hdr.TimeStamp.QuadPart;
        pEvent->ScreenTime = hdr.TimeStamp.QuadPart;
        pEvent->FinalState = PresentResult::Presented;

        // Sometimes, the queue packets associated with a present will complete
        // before the DxgKrnl PresentInfo event is fired.  For blit presents in
        // this case, we have no way to differentiate between fullscreen and
        // windowed blits, so we defer the completion of this present until
        // we've also seen the Dxgk Present_Info event.
        if (pEvent->SeenDxgkPresent || pEvent->PresentMode != PresentMode::Hardware_Legacy_Copy_To_Front_Buffer) {
            CompletePresent(pEvent);
        }
    }
}

// An MMIOFlip event is emitted when an MMIOFlip packet is dequeued.  All GPU
// work submitted prior to the flip has been completed.
//
// It also is emitted when an independent flip PHT is dequed, and will tell us
// whether the present is immediate or vsync.
void PMTraceConsumer::HandleDxgkMMIOFlip(EVENT_HEADER const& hdr, uint32_t flipSubmitSequence, uint32_t flags)
{
    auto eventIter = mPresentBySubmitSequence.find(flipSubmitSequence);
    if (eventIter == mPresentBySubmitSequence.end()) {
        return;
    }
    auto pEvent = eventIter->second;

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(pEvent);

    DebugModifyPresent(*pEvent);
    pEvent->ReadyTime = hdr.TimeStamp.QuadPart;

    if (pEvent->PresentMode == PresentMode::Composed_Flip) {
        pEvent->PresentMode = PresentMode::Hardware_Independent_Flip;
    }

    if (flags & (uint32_t) Microsoft_Windows_DxgKrnl::SetVidPnSourceAddressFlags::FlipImmediate) {
        pEvent->FinalState = PresentResult::Presented;
        pEvent->ScreenTime = hdr.TimeStamp.QuadPart;
        pEvent->SupportsTearing = true;
        if (pEvent->PresentMode == PresentMode::Hardware_Legacy_Flip) {
            CompletePresent(pEvent);
        }
    }
}

void PMTraceConsumer::HandleDxgkMMIOFlipMPO(EVENT_HEADER const& hdr, uint32_t flipSubmitSequence,
                                            uint32_t flipEntryStatusAfterFlip, bool flipEntryStatusAfterFlipValid)
{
    auto eventIter = mPresentBySubmitSequence.find(flipSubmitSequence);
    if (eventIter == mPresentBySubmitSequence.end()) {
        return;
    }
    auto pEvent = eventIter->second;

    TRACK_PRESENT_PATH(pEvent);

    // Avoid double-marking a single present packet coming from the MPO API
    if (pEvent->ReadyTime == 0) {
        DebugModifyPresent(*pEvent);
        pEvent->ReadyTime = hdr.TimeStamp.QuadPart;
    }

    if (!flipEntryStatusAfterFlipValid) {
        return;
    }

    TRACK_PRESENT_PATH(pEvent);

    // Present could tear if we're not waiting for vsync
    if (flipEntryStatusAfterFlip != (uint32_t) Microsoft_Windows_DxgKrnl::FlipEntryStatus::FlipWaitVSync) {
        DebugModifyPresent(*pEvent);
        pEvent->SupportsTearing = true;
    }

    // For the VSync ahd HSync paths, we'll wait for the corresponding ?SyncDPC
    // event before considering the present complete to get a more-accurate
    // ScreenTime (see HandleDxgkSyncDPC).
    if (flipEntryStatusAfterFlip == (uint32_t) Microsoft_Windows_DxgKrnl::FlipEntryStatus::FlipWaitVSync ||
        flipEntryStatusAfterFlip == (uint32_t) Microsoft_Windows_DxgKrnl::FlipEntryStatus::FlipWaitHSync) {
        return;
    }

    TRACK_PRESENT_PATH(pEvent);

    DebugModifyPresent(*pEvent);
    pEvent->FinalState = PresentResult::Presented;
    if (flipEntryStatusAfterFlip == (uint32_t) Microsoft_Windows_DxgKrnl::FlipEntryStatus::FlipWaitComplete) {
        pEvent->ScreenTime = hdr.TimeStamp.QuadPart;
    }

    if (pEvent->PresentMode == PresentMode::Hardware_Legacy_Flip) {
        CompletePresent(pEvent);
    }
}

void PMTraceConsumer::HandleDxgkSyncDPC(EVENT_HEADER const& hdr, uint32_t flipSubmitSequence)
{
    // The VSyncDPC/HSyncDPC contains a field telling us what flipped to screen.
    // This is the way to track completion of a fullscreen present.
    auto eventIter = mPresentBySubmitSequence.find(flipSubmitSequence);
    if (eventIter == mPresentBySubmitSequence.end()) {
        return;
    }
    auto pEvent = eventIter->second;

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(pEvent);

    DebugModifyPresent(*pEvent);
    pEvent->ScreenTime = hdr.TimeStamp.QuadPart;
    pEvent->FinalState = PresentResult::Presented;
    if (pEvent->PresentMode == PresentMode::Hardware_Legacy_Flip) {
        CompletePresent(pEvent);
    }
}

void PMTraceConsumer::HandleDxgkSyncDPCMPO(EVENT_HEADER const& hdr, uint32_t flipSubmitSequence, bool isMultiPlane)
{
    // The VSyncDPC/HSyncDPC contains a field telling us what flipped to screen.
    // This is the way to track completion of a fullscreen present.
    auto eventIter = mPresentBySubmitSequence.find(flipSubmitSequence);
    if (eventIter == mPresentBySubmitSequence.end()) {
        return;
    }
    auto pEvent = eventIter->second;

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(pEvent);

    if (isMultiPlane &&
        (pEvent->PresentMode == PresentMode::Hardware_Independent_Flip || pEvent->PresentMode == PresentMode::Composed_Flip)) {
        DebugModifyPresent(*pEvent);
        pEvent->PresentMode = PresentMode::Hardware_Composed_Independent_Flip;
    }

    // VSyncDPC and VSyncDPCMultiPlaneOverlay are both sent, with VSyncDPC only including flipSubmitSequence for one layer.
    // VSyncDPCMultiPlaneOverlay is sent afterward and contains info on whether this vsync/hsync contains an overlay.
    // So we should avoid updating ScreenTime and FinalState with the second event, but update isMultiPlane with the 
    // correct information when we have them.
    if (pEvent->FinalState != PresentResult::Presented) {
        DebugModifyPresent(*pEvent);
        pEvent->ScreenTime = hdr.TimeStamp.QuadPart;
        pEvent->FinalState = PresentResult::Presented;
    }

    CompletePresent(pEvent);
}

void PMTraceConsumer::HandleDxgkPresentHistory(
    EVENT_HEADER const& hdr,
    uint64_t token,
    uint64_t tokenData,
    PresentMode knownPresentMode)
{
    // These events are emitted during submission of all types of windowed presents while DWM is on.
    // It gives us up to two different types of keys to correlate further.

    // Lookup the in-progress present.  It should not have a known DxgkPresentHistoryToken
    // yet, so DxgkPresentHistoryToken!=0 implies we looked up a 'stuck' present whose
    // tracking was lost for some reason.
    auto presentEvent = FindOrCreatePresent(hdr);
    if (presentEvent == nullptr) {
        return;
    }

    if (presentEvent->DxgkPresentHistoryToken != 0) {
        RemoveLostPresent(presentEvent);
        presentEvent = FindOrCreatePresent(hdr);
        if (presentEvent == nullptr) {
            return;
        }

        assert(presentEvent->DxgkPresentHistoryToken == 0);
    }

    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(presentEvent);

    DebugModifyPresent(*presentEvent);
    presentEvent->ReadyTime = 0;
    presentEvent->ScreenTime = 0;
    presentEvent->SupportsTearing = false;
    presentEvent->FinalState = PresentResult::Unknown;
    presentEvent->DxgkPresentHistoryToken = token;

    auto iter = mPresentByDxgkPresentHistoryToken.find(token);
    if (iter != mPresentByDxgkPresentHistoryToken.end()) {
        RemoveLostPresent(iter->second);
    }
    assert(mPresentByDxgkPresentHistoryToken.find(token) == mPresentByDxgkPresentHistoryToken.end());
    mPresentByDxgkPresentHistoryToken[token] = presentEvent;

    if (presentEvent->PresentMode == PresentMode::Hardware_Legacy_Copy_To_Front_Buffer) {
        presentEvent->PresentMode = PresentMode::Composed_Copy_GPU_GDI;
        assert(knownPresentMode == PresentMode::Unknown ||
               knownPresentMode == PresentMode::Composed_Copy_GPU_GDI);

    } else if (presentEvent->PresentMode == PresentMode::Unknown) {
        if (knownPresentMode == PresentMode::Composed_Composition_Atlas) {
            presentEvent->PresentMode = PresentMode::Composed_Composition_Atlas;
        } else {
            // When there's no Win32K events, we'll assume PHTs that aren't after a blt, and aren't composition tokens
            // are flip tokens and that they're displayed. There are no Win32K events on Win7, and they might not be
            // present in some traces - don't let presents get stuck/dropped just because we can't track them perfectly.
            assert(!presentEvent->SeenWin32KEvents);
            presentEvent->PresentMode = PresentMode::Composed_Flip;
        }
    } else if (presentEvent->PresentMode == PresentMode::Composed_Copy_CPU_GDI) {
        if (tokenData == 0) {
            // This is the best we can do, we won't be able to tell how many frames are actually displayed.
            mPresentsWaitingForDWM.emplace_back(presentEvent);
            presentEvent->PresentInDwmWaitingStruct = true;
        } else {
            assert(mPresentByDxgkPresentHistoryTokenData.find(tokenData) == mPresentByDxgkPresentHistoryTokenData.end());
            mPresentByDxgkPresentHistoryTokenData[tokenData] = presentEvent;
            presentEvent->DxgkPresentHistoryTokenData = tokenData;
        }
    }

    // If we are not tracking further GPU/display-related events, complete the
    // present here.
    if (!mTrackDisplay) {
        CompletePresent(presentEvent);
    }
}

void PMTraceConsumer::HandleDxgkPresentHistoryInfo(EVENT_HEADER const& hdr, uint64_t token)
{
    // This event is emitted when a token is being handed off to DWM, and is a good way to indicate a ready state
    auto eventIter = mPresentByDxgkPresentHistoryToken.find(token);
    if (eventIter == mPresentByDxgkPresentHistoryToken.end()) {
        return;
    }

    DebugModifyPresent(*eventIter->second);
    TRACK_PRESENT_PATH_SAVE_GENERATED_ID(eventIter->second);

    eventIter->second->ReadyTime = eventIter->second->ReadyTime == 0
        ? hdr.TimeStamp.QuadPart
        : std::min(eventIter->second->ReadyTime, (uint64_t) hdr.TimeStamp.QuadPart);

    // Composed Composition Atlas or Win7 Flip does not have DWM events indicating intent to present this frame.
    if (eventIter->second->PresentMode == PresentMode::Composed_Composition_Atlas ||
        (eventIter->second->PresentMode == PresentMode::Composed_Flip && !eventIter->second->SeenWin32KEvents)) {
        mPresentsWaitingForDWM.emplace_back(eventIter->second);
        eventIter->second->PresentInDwmWaitingStruct = true;
        eventIter->second->DwmNotified = true;
    }

    if (eventIter->second->PresentMode == PresentMode::Composed_Copy_GPU_GDI) {
        // This present will be handed off to DWM.  When DWM is ready to
        // present, we'll query for the most recent blt targeting this window
        // and take it out of the map.
        mLastPresentByWindow[eventIter->second->Hwnd] = eventIter->second;
    }

    mPresentByDxgkPresentHistoryToken.erase(eventIter);
}

void PMTraceConsumer::HandleDXGKEvent(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto const& hdr = pEventRecord->EventHeader;
    switch (hdr.EventDescriptor.Id) {
    case Microsoft_Windows_DxgKrnl::Flip_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"FlipInterval" },
            { L"MMIOFlip" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto FlipInterval = desc[0].GetData<uint32_t>();
        auto MMIOFlip     = desc[1].GetData<BOOL>() != 0;

        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkFlip(hdr, FlipInterval, MMIOFlip);
        break;
    }
    case Microsoft_Windows_DxgKrnl::IndependentFlip_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"SubmitSequence" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto flipSubmitSequence = desc[0].GetData<uint32_t>();

        auto eventIter = mPresentBySubmitSequence.find(flipSubmitSequence);
        if (eventIter != mPresentBySubmitSequence.end()) {
            auto pEvent = eventIter->second;

            // We should not have already identified as hardware_composed - this
            // can only be detected around Vsync/HsyncDPC time.
            assert(pEvent->PresentMode != PresentMode::Hardware_Composed_Independent_Flip);

            DebugModifyPresent(*pEvent);
            pEvent->PresentMode = PresentMode::Hardware_Independent_Flip;
        }
        break;
    }
    case Microsoft_Windows_DxgKrnl::FlipMultiPlaneOverlay_Info::Id:
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkFlip(hdr, -1, true);
        break;
    case Microsoft_Windows_DxgKrnl::QueuePacket_Start::Id:
    {
        EventDataDesc desc[] = {
            { L"PacketType" },
            { L"SubmitSequence" },
            { L"hContext" },
            { L"bPresent" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto PacketType     = desc[0].GetData<uint32_t>();
        auto SubmitSequence = desc[1].GetData<uint32_t>();
        auto hContext       = desc[2].GetData<uint64_t>();
        auto bPresent       = desc[3].GetData<BOOL>() != 0;

        HandleDxgkQueueSubmit<false>(hdr, PacketType, SubmitSequence, hContext, bPresent);
        break;
    }
    case Microsoft_Windows_DxgKrnl::QueuePacket_Stop::Id:
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkQueueComplete(hdr, mMetadata.GetEventData<uint32_t>(pEventRecord, L"SubmitSequence"));
        break;
    case Microsoft_Windows_DxgKrnl::MMIOFlip_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"FlipSubmitSequence" },
            { L"Flags" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto FlipSubmitSequence = desc[0].GetData<uint32_t>();
        auto Flags              = desc[1].GetData<uint32_t>();

        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkMMIOFlip(hdr, FlipSubmitSequence, Flags);
        break;
    }
    case Microsoft_Windows_DxgKrnl::MMIOFlipMultiPlaneOverlay_Info::Id:
    {
        auto flipEntryStatusAfterFlipValid = hdr.EventDescriptor.Version >= 2;
        EventDataDesc desc[] = {
            { L"FlipSubmitSequence" },
            { L"FlipEntryStatusAfterFlip" }, // optional
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc) - (flipEntryStatusAfterFlipValid ? 0 : 1));
        auto FlipFenceId              = desc[0].GetData<uint64_t>();
        auto FlipEntryStatusAfterFlip = flipEntryStatusAfterFlipValid ? desc[1].GetData<uint32_t>() : 0u;

        auto flipSubmitSequence = (uint32_t) (FlipFenceId >> 32u);

        HandleDxgkMMIOFlipMPO(hdr, flipSubmitSequence, FlipEntryStatusAfterFlip, flipEntryStatusAfterFlipValid);
        break;
    }
    case Microsoft_Windows_DxgKrnl::VSyncDPCMultiPlane_Info::Id:
    case Microsoft_Windows_DxgKrnl::HSyncDPCMultiPlane_Info::Id:
    {
        // HSync is used for Hardware Independent Flip, and Hardware Composed
        // Flip to signal flipping to the screen on Windows 10 build numbers
        // 17134 and up where the associated display is connected to integrated
        // graphics
        //
        // MMIOFlipMPO [EntryStatus:FlipWaitHSync] -> HSync DPC

        TRACK_PRESENT_PATH_GENERATE_ID();

        EventDataDesc desc[] = {
            { L"PlaneCount" },
            { L"FlipEntryCount" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto PlaneCount = desc[0].GetData<uint32_t>();
        auto FlipCount  = desc[1].GetData<uint32_t>();

        if (FlipCount > 0) {
            // The number of active planes is determined by the number of non-zero
            // PresentIdOrPhysicalAddress (VSync) or ScannedPhysicalAddress (HSync)
            // properties.
            auto addressPropName = (hdr.EventDescriptor.Id == Microsoft_Windows_DxgKrnl::VSyncDPCMultiPlane_Info::Id && hdr.EventDescriptor.Version >= 1)
                ? L"PresentIdOrPhysicalAddress"
                : L"ScannedPhysicalAddress";

            uint32_t activePlaneCount = 0;
            for (uint32_t id = 0; id < PlaneCount; id++) {
                if (mMetadata.GetEventData<uint64_t>(pEventRecord, addressPropName, id) != 0) {
                    activePlaneCount++;
                }
            }

            auto isMultiPlane = activePlaneCount > 1;
            for (uint32_t i = 0; i < FlipCount; i++) {
                auto FlipId = mMetadata.GetEventData<uint64_t>(pEventRecord, L"FlipSubmitSequence", i);
                HandleDxgkSyncDPCMPO(hdr, (uint32_t)(FlipId >> 32u), isMultiPlane);
            }
        }
        break;
    }
    case Microsoft_Windows_DxgKrnl::VSyncDPC_Info::Id:
    {
        TRACK_PRESENT_PATH_GENERATE_ID();

        auto FlipFenceId = mMetadata.GetEventData<uint64_t>(pEventRecord, L"FlipFenceId");
        HandleDxgkSyncDPC(hdr, (uint32_t)(FlipFenceId >> 32u));
        break;
    }
    case Microsoft_Windows_DxgKrnl::Present_Info::Id:
    {
        // This event is emitted at the end of the kernel present, before returning.
        auto eventIter = mPresentByThreadId.find(hdr.ThreadId);
        if (eventIter != mPresentByThreadId.end()) {
            auto present = eventIter->second;

            TRACK_PRESENT_PATH(present);

            // Store the fact we've seen this present.  This is used to improve
            // tracking and to defer blt present completion until both Present_Info
            // and present QueuePacket_Stop have been seen.
            DebugModifyPresent(*present);
            present->SeenDxgkPresent = true;

            if (present->Hwnd == 0) {
                present->Hwnd = mMetadata.GetEventData<uint64_t>(pEventRecord, L"hWindow");
            }

            // If we are not expecting an API present end event, then this
            // should be the last operation on this thread.  This can happen
            // due to batched presents or non-instrumented present APIs (i.e.,
            // not DXGI nor D3D9).
            if (present->Runtime == Runtime::Other ||
                present->ThreadId != hdr.ThreadId) {
                mPresentByThreadId.erase(eventIter);
            }

            // If this is a deferred blit that's already seen QueuePacket_Stop,
            // then complete it now.
            if (present->PresentMode == PresentMode::Hardware_Legacy_Copy_To_Front_Buffer &&
                present->ScreenTime != 0) {
                CompletePresent(present);
            }
        }
        break;
    }
    case Microsoft_Windows_DxgKrnl::PresentHistoryDetailed_Start::Id:
    case Microsoft_Windows_DxgKrnl::PresentHistory_Start::Id:
    {
        EventDataDesc desc[] = {
            { L"Token" },
            { L"Model" },
            { L"TokenData" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto Token     = desc[0].GetData<uint64_t>();
        auto Model     = desc[1].GetData<Microsoft_Windows_DxgKrnl::PresentModel>();
        auto TokenData = desc[2].GetData<uint64_t>();

        if (Model != Microsoft_Windows_DxgKrnl::PresentModel::D3DKMT_PM_REDIRECTED_GDI) {
            auto presentMode = PresentMode::Unknown;
            switch (Model) {
            case Microsoft_Windows_DxgKrnl::PresentModel::D3DKMT_PM_REDIRECTED_BLT:         presentMode = PresentMode::Composed_Copy_GPU_GDI; break;
            case Microsoft_Windows_DxgKrnl::PresentModel::D3DKMT_PM_REDIRECTED_VISTABLT:    presentMode = PresentMode::Composed_Copy_CPU_GDI; break;
            case Microsoft_Windows_DxgKrnl::PresentModel::D3DKMT_PM_REDIRECTED_FLIP:        presentMode = PresentMode::Composed_Flip; break;
            case Microsoft_Windows_DxgKrnl::PresentModel::D3DKMT_PM_REDIRECTED_COMPOSITION: presentMode = PresentMode::Composed_Composition_Atlas; break;
            }

            TRACK_PRESENT_PATH_GENERATE_ID();
            HandleDxgkPresentHistory(hdr, Token, TokenData, presentMode);
        }
        break;
    }
    case Microsoft_Windows_DxgKrnl::PresentHistory_Info::Id:
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkPresentHistoryInfo(hdr, mMetadata.GetEventData<uint64_t>(pEventRecord, L"Token"));
        break;
    case Microsoft_Windows_DxgKrnl::Blit_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"hwnd" },
            { L"bRedirectedPresent" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto hwnd               = desc[0].GetData<uint64_t>();
        auto bRedirectedPresent = desc[1].GetData<uint32_t>() != 0;

        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkBlt(hdr, hwnd, bRedirectedPresent);
        break;
    }
    case Microsoft_Windows_DxgKrnl::Blit_Cancel::Id:
        HandleDxgkBltCancel(hdr);
        break;
    default:
        assert(!mFilteredEvents); // Assert that filtering is working if expected
        break;
    }
}

namespace Win7 {

typedef LARGE_INTEGER PHYSICAL_ADDRESS;

#pragma pack(push)
#pragma pack(1)

typedef struct _DXGKETW_BLTEVENT {
    ULONGLONG                  hwnd;
    ULONGLONG                  pDmaBuffer;
    ULONGLONG                  PresentHistoryToken;
    ULONGLONG                  hSourceAllocation;
    ULONGLONG                  hDestAllocation;
    BOOL                       bSubmit;
    BOOL                       bRedirectedPresent;
    UINT                       Flags; // DXGKETW_PRESENTFLAGS
    RECT                       SourceRect;
    RECT                       DestRect;
    UINT                       SubRectCount; // followed by variable number of ETWGUID_DXGKBLTRECT events
} DXGKETW_BLTEVENT;

typedef struct _DXGKETW_FLIPEVENT {
    ULONGLONG                  pDmaBuffer;
    ULONG                      VidPnSourceId;
    ULONGLONG                  FlipToAllocation;
    UINT                       FlipInterval; // D3DDDI_FLIPINTERVAL_TYPE
    BOOLEAN                    FlipWithNoWait;
    BOOLEAN                    MMIOFlip;
} DXGKETW_FLIPEVENT;

typedef struct _DXGKETW_PRESENTHISTORYEVENT {
    ULONGLONG             hAdapter;
    ULONGLONG             Token;
    ULONG                 Model;     // available only for _STOP event type.
    UINT                  TokenSize; // available only for _STOP event type.
} DXGKETW_PRESENTHISTORYEVENT;

typedef struct _DXGKETW_QUEUESUBMITEVENT {
    ULONGLONG                  hContext;
    ULONG                      PacketType; // DXGKETW_QUEUE_PACKET_TYPE
    ULONG                      SubmitSequence;
    ULONGLONG                  DmaBufferSize;
    UINT                       AllocationListSize;
    UINT                       PatchLocationListSize;
    BOOL                       bPresent;
    ULONGLONG                  hDmaBuffer;
} DXGKETW_QUEUESUBMITEVENT;

typedef struct _DXGKETW_QUEUECOMPLETEEVENT {
    ULONGLONG                  hContext;
    ULONG                      PacketType;
    ULONG                      SubmitSequence;
    union {
        BOOL                   bPreempted;
        BOOL                   bTimeouted; // PacketType is WaitCommandBuffer.
    };
} DXGKETW_QUEUECOMPLETEEVENT;

typedef struct _DXGKETW_SCHEDULER_VSYNC_DPC {
    ULONGLONG                 pDxgAdapter;
    UINT                      VidPnTargetId;
    PHYSICAL_ADDRESS          ScannedPhysicalAddress;
    UINT                      VidPnSourceId;
    UINT                      FrameNumber;
    LONGLONG                  FrameQPCTime;
    ULONGLONG                 hFlipDevice;
    UINT                      FlipType; // DXGKETW_FLIPMODE_TYPE
    union
    {
        ULARGE_INTEGER        FlipFenceId;
        PHYSICAL_ADDRESS      FlipToAddress;
    };
} DXGKETW_SCHEDULER_VSYNC_DPC;

typedef struct _DXGKETW_SCHEDULER_MMIO_FLIP_32 {
    ULONGLONG        pDxgAdapter;
    UINT             VidPnSourceId;
    ULONG            FlipSubmitSequence; // ContextUserSubmissionId
    UINT             FlipToDriverAllocation;
    PHYSICAL_ADDRESS FlipToPhysicalAddress;
    UINT             FlipToSegmentId;
    UINT             FlipPresentId;
    UINT             FlipPhysicalAdapterMask;
    ULONG            Flags;
} DXGKETW_SCHEDULER_MMIO_FLIP_32;

typedef struct _DXGKETW_SCHEDULER_MMIO_FLIP_64 {
    ULONGLONG        pDxgAdapter;
    UINT             VidPnSourceId;
    ULONG            FlipSubmitSequence; // ContextUserSubmissionId
    ULONGLONG        FlipToDriverAllocation;
    PHYSICAL_ADDRESS FlipToPhysicalAddress;
    UINT             FlipToSegmentId;
    UINT             FlipPresentId;
    UINT             FlipPhysicalAdapterMask;
    ULONG            Flags;
} DXGKETW_SCHEDULER_MMIO_FLIP_64;

#pragma pack(pop)

} // namespace Win7

void PMTraceConsumer::HandleWin7DxgkBlt(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);
    TRACK_PRESENT_PATH_GENERATE_ID();

    auto pBltEvent = reinterpret_cast<Win7::DXGKETW_BLTEVENT*>(pEventRecord->UserData);
    HandleDxgkBlt(
        pEventRecord->EventHeader,
        pBltEvent->hwnd,
        pBltEvent->bRedirectedPresent != 0);
}

void PMTraceConsumer::HandleWin7DxgkFlip(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);
    TRACK_PRESENT_PATH_GENERATE_ID();

    auto pFlipEvent = reinterpret_cast<Win7::DXGKETW_FLIPEVENT*>(pEventRecord->UserData);
    HandleDxgkFlip(
        pEventRecord->EventHeader,
        pFlipEvent->FlipInterval,
        pFlipEvent->MMIOFlip != 0);
}

void PMTraceConsumer::HandleWin7DxgkPresentHistory(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto pPresentHistoryEvent = reinterpret_cast<Win7::DXGKETW_PRESENTHISTORYEVENT*>(pEventRecord->UserData);
    if (pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_START) {
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkPresentHistory(
            pEventRecord->EventHeader,
            pPresentHistoryEvent->Token,
            0,
            PresentMode::Unknown);
    } else if (pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_INFO) {
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkPresentHistoryInfo(pEventRecord->EventHeader, pPresentHistoryEvent->Token);
    }
}

void PMTraceConsumer::HandleWin7DxgkQueuePacket(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    if (pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_START) {
        auto pSubmitEvent = reinterpret_cast<Win7::DXGKETW_QUEUESUBMITEVENT*>(pEventRecord->UserData);
        HandleDxgkQueueSubmit<true>(
            pEventRecord->EventHeader,
            pSubmitEvent->PacketType,
            pSubmitEvent->SubmitSequence,
            pSubmitEvent->hContext,
            pSubmitEvent->bPresent != 0);
    } else if (pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_STOP) {
        auto pCompleteEvent = reinterpret_cast<Win7::DXGKETW_QUEUECOMPLETEEVENT*>(pEventRecord->UserData);
        TRACK_PRESENT_PATH_GENERATE_ID();
        HandleDxgkQueueComplete(pEventRecord->EventHeader, pCompleteEvent->SubmitSequence);
    }
}

void PMTraceConsumer::HandleWin7DxgkVSyncDPC(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);
    TRACK_PRESENT_PATH_GENERATE_ID();

    auto pVSyncDPCEvent = reinterpret_cast<Win7::DXGKETW_SCHEDULER_VSYNC_DPC*>(pEventRecord->UserData);

    // Windows 7 does not support MultiPlaneOverlay.
    HandleDxgkSyncDPC(pEventRecord->EventHeader, (uint32_t)(pVSyncDPCEvent->FlipFenceId.QuadPart >> 32u));
}

void PMTraceConsumer::HandleWin7DxgkMMIOFlip(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);
    TRACK_PRESENT_PATH_GENERATE_ID();

    if (pEventRecord->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER)
    {
        auto pMMIOFlipEvent = reinterpret_cast<Win7::DXGKETW_SCHEDULER_MMIO_FLIP_32*>(pEventRecord->UserData);
        HandleDxgkMMIOFlip(
            pEventRecord->EventHeader,
            pMMIOFlipEvent->FlipSubmitSequence,
            pMMIOFlipEvent->Flags);
    }
    else
    {
        auto pMMIOFlipEvent = reinterpret_cast<Win7::DXGKETW_SCHEDULER_MMIO_FLIP_64*>(pEventRecord->UserData);
        HandleDxgkMMIOFlip(
            pEventRecord->EventHeader,
            pMMIOFlipEvent->FlipSubmitSequence,
            pMMIOFlipEvent->Flags);
    }
}

void PMTraceConsumer::HandleWin32kEvent(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto const& hdr = pEventRecord->EventHeader;
    switch (hdr.EventDescriptor.Id) {
    case Microsoft_Windows_Win32k::TokenCompositionSurfaceObject_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"CompositionSurfaceLuid" },
            { L"PresentCount" },
            { L"BindId" },
            { L"DestWidth" },  // version >= 1
            { L"DestHeight" }, // version >= 1
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc) - (hdr.EventDescriptor.Version == 0 ? 2 : 0));
        auto CompositionSurfaceLuid = desc[0].GetData<uint64_t>();
        auto PresentCount           = desc[1].GetData<uint64_t>();
        auto BindId                 = desc[2].GetData<uint64_t>();

        // Lookup the in-progress present.  It should not have seen any Win32K
        // events yet, so SeenWin32KEvents==true implies we looked up a 'stuck'
        // present whose tracking was lost for some reason.
        auto PresentEvent = FindOrCreatePresent(hdr);
        if (PresentEvent == nullptr) {
            return;
        }

        if (PresentEvent->SeenWin32KEvents) {
            RemoveLostPresent(PresentEvent);
            PresentEvent = FindOrCreatePresent(hdr);
            if (PresentEvent == nullptr) {
                return;
            }

            assert(!PresentEvent->SeenWin32KEvents);
        }

        TRACK_PRESENT_PATH(PresentEvent);

        PresentEvent->PresentMode = PresentMode::Composed_Flip;
        PresentEvent->SeenWin32KEvents = true;

        if (hdr.EventDescriptor.Version >= 1) {
            PresentEvent->DestWidth  = desc[3].GetData<uint32_t>();
            PresentEvent->DestHeight = desc[4].GetData<uint32_t>();
        }

        Win32KPresentHistoryToken key(CompositionSurfaceLuid, PresentCount, BindId);
        assert(mPresentByWin32KPresentHistoryToken.find(key) == mPresentByWin32KPresentHistoryToken.end());
        mPresentByWin32KPresentHistoryToken[key] = PresentEvent;
        PresentEvent->CompositionSurfaceLuid = CompositionSurfaceLuid;
        PresentEvent->Win32KPresentCount = PresentCount;
        PresentEvent->Win32KBindId = BindId;
        break;
    }

    case Microsoft_Windows_Win32k::TokenStateChanged_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"CompositionSurfaceLuid" },
            { L"PresentCount" },
            { L"BindId" },
            { L"NewState" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto CompositionSurfaceLuid = desc[0].GetData<uint64_t>();
        auto PresentCount           = desc[1].GetData<uint32_t>();
        auto BindId                 = desc[2].GetData<uint64_t>();
        auto NewState               = desc[3].GetData<uint32_t>();

        Win32KPresentHistoryToken key(CompositionSurfaceLuid, PresentCount, BindId);
        auto eventIter = mPresentByWin32KPresentHistoryToken.find(key);
        if (eventIter == mPresentByWin32KPresentHistoryToken.end()) {
            return;
        }
        auto presentEvent = eventIter->second;

        switch (NewState) {
        case (uint32_t) Microsoft_Windows_Win32k::TokenState::InFrame: // Composition is starting
        {
            TRACK_PRESENT_PATH(presentEvent);

            // We won't necessarily see a transition to Discarded for all
            // presents. If we're compositing a newer present than the window's
            // last known present, then the last known one was discarded.
            if (presentEvent->Hwnd) {
                auto hWndIter = mLastPresentByWindow.find(presentEvent->Hwnd);
                if (hWndIter == mLastPresentByWindow.end()) {
                    mLastPresentByWindow.emplace(presentEvent->Hwnd, presentEvent);
                } else if (hWndIter->second != presentEvent) {
                    DebugModifyPresent(*hWndIter->second);
                    hWndIter->second->FinalState = PresentResult::Discarded;    // TODO: Complete the present here?
                    hWndIter->second = presentEvent;
                }
            }

            DebugModifyPresent(*presentEvent);
            presentEvent->SeenInFrameEvent = true;

            bool iFlip = mMetadata.GetEventData<BOOL>(pEventRecord, L"IndependentFlip") != 0;
            if (iFlip && presentEvent->PresentMode == PresentMode::Composed_Flip) {
                presentEvent->PresentMode = PresentMode::Hardware_Independent_Flip;
            }
            break;
        }

        case (uint32_t) Microsoft_Windows_Win32k::TokenState::Confirmed: // Present has been submitted
            TRACK_PRESENT_PATH(presentEvent);

            // Handle DO_NOT_SEQUENCE presents, which may get marked as confirmed,
            // if a frame was composed when this token was completed
            if (presentEvent->FinalState == PresentResult::Unknown &&
                (presentEvent->PresentFlags & DXGI_PRESENT_DO_NOT_SEQUENCE) != 0) {
                DebugModifyPresent(*presentEvent);
                presentEvent->FinalState = PresentResult::Discarded;
            }
            if (presentEvent->Hwnd) {
                mLastPresentByWindow.erase(presentEvent->Hwnd);
            }
            break;

        // Note: Going forward, TokenState::Retired events are no longer
        // guaranteed to be sent at the end of a frame in multi-monitor
        // scenarios.  Instead, we use DWM's present stats to understand the
        // Composed Flip timeline.
        case (uint32_t) Microsoft_Windows_Win32k::TokenState::Discarded: // Present has been discarded
        {
            TRACK_PRESENT_PATH(presentEvent);

            mPresentByWin32KPresentHistoryToken.erase(eventIter);

            if (!presentEvent->SeenInFrameEvent && (presentEvent->FinalState == PresentResult::Unknown || presentEvent->ScreenTime == 0)) {
                DebugModifyPresent(*presentEvent);
                presentEvent->FinalState = PresentResult::Discarded;
                CompletePresent(presentEvent);
            } else if (presentEvent->PresentMode != PresentMode::Composed_Flip) {
                CompletePresent(presentEvent);
            }

            break;
        }
        }
        break;
    }
    default:
        assert(!mFilteredEvents); // Assert that filtering is working if expected
        break;
    }
}

void PMTraceConsumer::HandleDWMEvent(EVENT_RECORD* pEventRecord)
{
    DebugEvent(pEventRecord, &mMetadata);

    auto const& hdr = pEventRecord->EventHeader;
    switch (hdr.EventDescriptor.Id) {
    case Microsoft_Windows_Dwm_Core::MILEVENT_MEDIA_UCE_PROCESSPRESENTHISTORY_GetPresentHistory_Info::Id:
        // Move all the latest in-progress Composed_Copy from each window into
        // mPresentsWaitingForDWM, to be attached to the next DWM present's
        // DependentPresents.
        for (auto& hWndPair : mLastPresentByWindow) {
            auto& present = hWndPair.second;
            if (present->PresentMode == PresentMode::Composed_Copy_GPU_GDI ||
                present->PresentMode == PresentMode::Composed_Copy_CPU_GDI) {
                TRACK_PRESENT_PATH(present);
                DebugModifyPresent(*present);
                present->DwmNotified = true;
                mPresentsWaitingForDWM.emplace_back(present);
                present->PresentInDwmWaitingStruct = true;
            }
        }
        mLastPresentByWindow.clear();
        break;

    case Microsoft_Windows_Dwm_Core::SCHEDULE_PRESENT_Start::Id:
        DwmProcessId = hdr.ProcessId;
        DwmPresentThreadId = hdr.ThreadId;
        break;

    // These events are only used for Composed_Copy_CPU_GDI presents.  They are
    // used to identify when such presents are handed off to DWM. 
    case Microsoft_Windows_Dwm_Core::FlipChain_Pending::Id:
    case Microsoft_Windows_Dwm_Core::FlipChain_Complete::Id:
    case Microsoft_Windows_Dwm_Core::FlipChain_Dirty::Id:
    {
        if (InlineIsEqualGUID(hdr.ProviderId, Microsoft_Windows_Dwm_Core::Win7::GUID)) {
            break;
        }

        EventDataDesc desc[] = {
            { L"ulFlipChain" },
            { L"ulSerialNumber" },
            { L"hwnd" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto ulFlipChain    = desc[0].GetData<uint32_t>();
        auto ulSerialNumber = desc[1].GetData<uint32_t>();
        auto hwnd           = desc[2].GetData<uint64_t>();

        // Lookup the present using the 64-bit token data from the PHT
        // submission, which is actually two 32-bit data chunks corresponding
        // to a flip chain id and present id.
        auto tokenData = ((uint64_t) ulFlipChain << 32ull) | ulSerialNumber;
        auto flipIter = mPresentByDxgkPresentHistoryTokenData.find(tokenData);
        if (flipIter != mPresentByDxgkPresentHistoryTokenData.end()) {
            auto present = flipIter->second;

            TRACK_PRESENT_PATH(present);

            DebugModifyPresent(*present);
            present->DxgkPresentHistoryTokenData = 0;
            present->DwmNotified = true;

            mLastPresentByWindow[hwnd] = present;

            mPresentByDxgkPresentHistoryTokenData.erase(flipIter);
        }
        break;
    }
    case Microsoft_Windows_Dwm_Core::SCHEDULE_SURFACEUPDATE_Info::Id:
    {
        EventDataDesc desc[] = {
            { L"luidSurface" },
            { L"PresentCount" },
            { L"bindId" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));
        auto luidSurface  = desc[0].GetData<uint64_t>();
        auto PresentCount = desc[1].GetData<uint64_t>();
        auto bindId       = desc[2].GetData<uint64_t>();

        Win32KPresentHistoryToken key(luidSurface, PresentCount, bindId);
        auto eventIter = mPresentByWin32KPresentHistoryToken.find(key);
        if (eventIter != mPresentByWin32KPresentHistoryToken.end() && eventIter->second->SeenInFrameEvent) {
            TRACK_PRESENT_PATH(eventIter->second);
            DebugModifyPresent(*eventIter->second);
            eventIter->second->DwmNotified = true;
            mPresentsWaitingForDWM.emplace_back(eventIter->second);
            eventIter->second->PresentInDwmWaitingStruct = true;
        }
        break;
    }
    default:
        assert(!mFilteredEvents || // Assert that filtering is working if expected
               hdr.ProviderId == Microsoft_Windows_Dwm_Core::Win7::GUID);
        break;
    }
}

// Remove the present from all temporary tracking structures.
void PMTraceConsumer::RemovePresentFromTemporaryTrackingCollections(std::shared_ptr<PresentEvent> p, bool waitForPresentStop)
{
    // mAllPresents
    if (p->mAllPresentsTrackingIndex != UINT32_MAX) {
        mAllPresents[p->mAllPresentsTrackingIndex] = nullptr;
    }

    // mPresentByThreadId
    //
    // If the present was batched, it will by referenced in mPresentByThreadId
    // by both ThreadId and DriverBatchThreadId.
    //
    // There are cases where a present can be displayed/dropped before
    // Present() returns, in which case we defer completion and leave the
    // PresentEvent in mPresentByThreadId so that subsequent Present_Stop
    // events can still look it up.
    if (!waitForPresentStop) {
        auto threadEventIter = mPresentByThreadId.find(p->ThreadId);
        if (threadEventIter != mPresentByThreadId.end() && threadEventIter->second == p) {
            mPresentByThreadId.erase(threadEventIter);
        }
    }
    if (p->DriverBatchThreadId != 0) {
        auto threadEventIter = mPresentByThreadId.find(p->DriverBatchThreadId);
        if (threadEventIter != mPresentByThreadId.end() && threadEventIter->second == p) {
            mPresentByThreadId.erase(threadEventIter);
        }
    }

    // mOrderedPresentsByProcessId
    mOrderedPresentsByProcessId[p->ProcessId].erase(p->QpcTime);

    // mPresentBySubmitSequence
    if (p->QueueSubmitSequence != 0) {
        auto eventIter = mPresentBySubmitSequence.find(p->QueueSubmitSequence);
        if (eventIter != mPresentBySubmitSequence.end() && (eventIter->second == p)) {
            mPresentBySubmitSequence.erase(eventIter);
        }
    }

    // mPresentByWin32KPresentHistoryToken
    if (p->CompositionSurfaceLuid != 0) {
        Win32KPresentHistoryToken key(
            p->CompositionSurfaceLuid,
            p->Win32KPresentCount,
            p->Win32KBindId
        );

        auto eventIter = mPresentByWin32KPresentHistoryToken.find(key);
        if (eventIter != mPresentByWin32KPresentHistoryToken.end() && (eventIter->second == p)) {
            mPresentByWin32KPresentHistoryToken.erase(eventIter);
        }
    }

    // mPresentByDxgkPresentHistoryToken
    if (p->DxgkPresentHistoryToken != 0) {
        auto eventIter = mPresentByDxgkPresentHistoryToken.find(p->DxgkPresentHistoryToken);
        if (eventIter != mPresentByDxgkPresentHistoryToken.end() && eventIter->second == p) {
            mPresentByDxgkPresentHistoryToken.erase(eventIter);
        }
    }

    // mPresentByDxgkPresentHistoryTokenData
    if (p->DxgkPresentHistoryTokenData != 0) {
        auto eventIter = mPresentByDxgkPresentHistoryTokenData.find(p->DxgkPresentHistoryTokenData);
        if (eventIter != mPresentByDxgkPresentHistoryTokenData.end() && eventIter->second == p) {
            mPresentByDxgkPresentHistoryTokenData.erase(eventIter);
        }
    }

    // mPresentByDxgkContext
    if (p->DxgkContext != 0) {
        auto eventIter = mPresentByDxgkContext.find(p->DxgkContext);
        if (eventIter != mPresentByDxgkContext.end() && eventIter->second == p) {
            mPresentByDxgkContext.erase(eventIter);
        }
    }

    // mLastPresentByWindow
    if (p->Hwnd != 0) {
        auto eventIter = mLastPresentByWindow.find(p->Hwnd);
        if (eventIter != mLastPresentByWindow.end() && eventIter->second == p) {
            mLastPresentByWindow.erase(eventIter);
        }
    }

    // mPresentsWaitingForDWM
    if (p->PresentInDwmWaitingStruct) {
        for (auto presentIter = mPresentsWaitingForDWM.begin(); presentIter != mPresentsWaitingForDWM.end(); presentIter++) {
            if (p == *presentIter) {
                mPresentsWaitingForDWM.erase(presentIter);
                p->PresentInDwmWaitingStruct = false;
                break;
            }
        }
    }
}

void PMTraceConsumer::RemoveLostPresent(std::shared_ptr<PresentEvent> p)
{
    // If this is a DWM present, any other presents that contributed to it are
    // also lost.
    //
    // PresentEvents that become lost are not removed from DependentPresents
    // tracking, so we need to protect against lost events (but they have
    // already been added to mLostPresentEvents etc.).
    for (auto& p2 : p->DependentPresents) {
        if (!p2->IsLost) {
            RemoveLostPresent(p2);
        }
    }
    p->DependentPresents.clear();

    // Remove the present from any tracking structures.
    auto waitForPresentStop = false;
    RemovePresentFromTemporaryTrackingCollections(p, waitForPresentStop);

    // Move the present into the consumer lost queue.
    DebugLostPresent(*p);
    p->IsLost = true;
    {
        std::lock_guard<std::mutex> lock(mLostPresentEventMutex);
        mLostPresentEvents.push_back(p);
    }
}

void PMTraceConsumer::CompletePresentHelper(std::shared_ptr<PresentEvent> const& p, OrderedPresents* completed)
{
    // We use the first completed present to indicate that all necessary
    // providers are running and able to successfully track/complete presents.
    //
    // At the first completion, there may be numerous presents that have been
    // created but not properly tracked due to missed events.  This is
    // especially prevalent in ETLs that start runtime providers before backend
    // providers and/or start capturing while an intensive graphics application
    // is already running.  When that happens, QpcTime/TimeTaken and
    // ReadyTime/ScreenTime times can become mis-matched, and that offset can
    // persist for the full capture.
    //
    // We handle this by throwing away all queued presents up to this point.
    if (!mHasCompletedAPresent) {
        mHasCompletedAPresent = true;

        for (auto const& pr : mOrderedPresentsByProcessId) {
            auto processPresents = &pr.second;
            for (auto ii = processPresents->begin(), ie = processPresents->end(); ii != ie; ) {
                auto p2 = ii->second;
                ++ii; // Increment before calling RemoveLostPresent(), which removes from processPresents
                RemoveLostPresent(p2);
            }
        }
        return;
    }

    // Skip processing if compltion is already deferred.
    if (p->CompletionIsDeferred) {
        return;
    }

    // Remove the present from any tracking structures.
    auto waitForPresentStop = p->Runtime != Runtime::Other && p->TimeTaken == 0;
    RemovePresentFromTemporaryTrackingCollections(p, waitForPresentStop);

    // If presented, remove any previous presents made on the same swap chain.
    //
    // We need to do this before the DWM dependencies to ensure dependent
    // presents are completed twice.  If there is a previous DWM present there
    // is also likely a previous dependent present and if we complete
    // dependents first, we will Complete p1 twice.
    //
    // | Dependent | DWM |
    // | p1        |     |
    // |           | p2  |
    // | p3        |     |
    // |           | p4  |
    if (p->FinalState == PresentResult::Presented) {
        auto presentsByThisProcess = &mOrderedPresentsByProcessId[p->ProcessId];
        for (auto ii = presentsByThisProcess->begin(), ie = presentsByThisProcess->end(); ii != ie; ) {
            auto p2 = ii->second;
            ++ii; // increment iterator first as CompletePresentHelper() will remove it
            if (p2->SwapChainAddress == p->SwapChainAddress) {
                if (p2->QpcTime >= p->QpcTime) break;
                CompletePresentHelper(p2, completed);
            }
        }
    }

    // If this is a DWM present, complete any other present that contributed to
    // it.  Each DWM present only completes each HWND's most recent Composed
    // Flip PresentEvent, so we mark any others as discarded first, then
    // proceed in order completing them all.
    //
    // PresentEvents that become lost are not removed from DependentPresents
    // tracking, so we need to protect against lost events (but they have
    // already been added to mLostPresentEvents etc.).
    std::unordered_set<uint64_t> completedComposedFlipHwnds;
    for (auto ii = p->DependentPresents.rbegin(), ie = p->DependentPresents.rend(); ii != ie; ++ii) {
        auto p2 = *ii;
        if (!p2->IsLost && p2->PresentMode == PresentMode::Composed_Flip && !completedComposedFlipHwnds.emplace(p2->Hwnd).second) {
            DebugModifyPresent(*p2);
            p2->FinalState = PresentResult::Discarded;
        }
    }
    completedComposedFlipHwnds.clear();
    for (auto p2 : p->DependentPresents) {
        if (!p2->IsLost && p2->FinalState != PresentResult::Discarded) {
            DebugModifyPresent(*p2);
            p2->FinalState = p->FinalState;
            p2->ScreenTime = p->ScreenTime;
        }
        CompletePresentHelper(p2, completed);
    }
    p->DependentPresents.clear();

    // The PresentEvent is now removed from all tracking structures and we can
    // move it into the consumer thread queue. If it is still missing some
    // expected events, we instead place it into the DeferredCompletions list
    // where wait for some number of presents before moving into the consumer
    // thread queue.
    auto deferredWaitCount = GetDeferredCompletionWaitCount(*p);
    if (deferredWaitCount > 0) {
        DebugModifyPresent(*p);
        p->CompletionIsDeferred = true;
        mDeferredCompletions[p->ProcessId].emplace_back(p, deferredWaitCount);
    } else {
        DebugModifyPresent(*p);
        p->IsCompleted = true;
        completed->emplace(p->QpcTime, p);
    }
}

void PMTraceConsumer::CompletePresent(std::shared_ptr<PresentEvent> const& p)
{
    // CompletePresentHelper() will complete the present and any of its
    // dependencies.  We collect all completed presents into an OrderedPresents
    // because the dependency walk only respects ordering for presents on the
    // same SwapChain.
    OrderedPresents completed;
    CompletePresentHelper(p, &completed);

    // Move the completed presents into the consumer thread queue.
    auto numCompleted = completed.size();
    if (numCompleted > 0) {
        std::lock_guard<std::mutex> lock(mPresentEventMutex);
        mCompletePresentEvents.reserve(mCompletePresentEvents.size() + numCompleted);
        for (auto const& tuple : completed) {
            mCompletePresentEvents.push_back(tuple.second);
        }
    }
}

void PMTraceConsumer::CompleteDeferredCompletion(std::shared_ptr<PresentEvent> const& present)
{
    assert(present->CompletionIsDeferred == true);
    assert(present->IsCompleted == false);

    auto threadEventIter = mPresentByThreadId.find(present->ThreadId);
    if (threadEventIter != mPresentByThreadId.end() && threadEventIter->second == present) {
        mPresentByThreadId.erase(threadEventIter);
    }

    DebugModifyPresent(*present);
    present->IsCompleted = true;

    {
        std::lock_guard<std::mutex> lock(mPresentEventMutex);
        mCompletePresentEvents.emplace_back(present);
    }
}

std::shared_ptr<PresentEvent> PMTraceConsumer::FindOrCreatePresent(EVENT_HEADER const& hdr)
{
    // Check if there is an in-progress present that this thread is already
    // working on and, if so, continue working on that.
    auto threadEventIter = mPresentByThreadId.find(hdr.ThreadId);
    if (threadEventIter != mPresentByThreadId.end()) {
        return threadEventIter->second;
    }

    // If not, check if this event is from a process that is filtered out and,
    // if so, ignore it.
    if (!IsProcessTrackedForFiltering(hdr.ProcessId)) {
        return nullptr;
    }

    // Search for an in-progress present created by this process that still
    // doesn't have a known PresentMode.  This can be the case for DXGI/D3D
    // presents created on a different thread, which are batched and then
    // handled later during a DXGK/Win32K event.  If found, we add it to
    // mPresentByThreadId to indicate what present this thread is working on.
    auto presentsByThisProcess = &mOrderedPresentsByProcessId[hdr.ProcessId];
    for (auto const& tuple : *presentsByThisProcess) {
        auto presentEvent = tuple.second;
        if (presentEvent->PresentMode == PresentMode::Unknown) {
            assert(presentEvent->DriverBatchThreadId == 0);
            DebugModifyPresent(*presentEvent);
            presentEvent->DriverBatchThreadId = hdr.ThreadId;
            mPresentByThreadId.emplace(hdr.ThreadId, presentEvent);
            return presentEvent;
        }
    }

    // Because we couldn't find a present above, the calling event is for an
    // unknown, in-progress present.  This can happen if the present didn't
    // originate from a runtime whose events we're tracking (i.e., DXGI or
    // D3D9) in which case a DXGKRNL event will be the first present-related
    // event we ever see.  So, we create the PresentEvent and start tracking it
    // from here.
    auto presentEvent = std::make_shared<PresentEvent>(hdr, Runtime::Other);
    TrackPresent(presentEvent, presentsByThisProcess);
    return presentEvent;
}

void PMTraceConsumer::TrackPresent(
    std::shared_ptr<PresentEvent> present,
    OrderedPresents* presentsByThisProcess)
{
    // If there is an existing present that hasn't completed by the time the
    // circular buffer has come around, consider it lost.
    if (mAllPresents[mAllPresentsNextIndex] != nullptr) {
        RemoveLostPresent(mAllPresents[mAllPresentsNextIndex]);
    }

    DebugCreatePresent(*present);
    present->mAllPresentsTrackingIndex = mAllPresentsNextIndex;
    mAllPresents[mAllPresentsNextIndex] = present;
    mAllPresentsNextIndex = (mAllPresentsNextIndex + 1) % PRESENTEVENT_CIRCULAR_BUFFER_SIZE;

    presentsByThisProcess->emplace(present->QpcTime, present);
    mPresentByThreadId.emplace(present->ThreadId, present);
}

void PMTraceConsumer::TrackPresentOnThread(std::shared_ptr<PresentEvent> present)
{
    // If there is an in-flight present on this thread already, then something
    // has gone wrong with it's tracking so consider it lost.
    auto iter = mPresentByThreadId.find(present->ThreadId);
    if (iter != mPresentByThreadId.end()) {
        RemoveLostPresent(iter->second);
    }

    TrackPresent(present, &mOrderedPresentsByProcessId[present->ProcessId]);
}

// No TRACK_PRESENT instrumentation here because each runtime Present::Start
// event is instrumented and we assume we'll see the corresponding Stop event
// for any completed present.
void PMTraceConsumer::RuntimePresentStop(EVENT_HEADER const& hdr, bool AllowPresentBatching, Runtime runtime)
{
    // Lookup the PresentEvent most-recently operated on by the same thread.
    // If there isn't one, ignore this event.
    auto eventIter = mPresentByThreadId.find(hdr.ThreadId);
    if (eventIter != mPresentByThreadId.end()) {
        auto present = eventIter->second;

        DebugModifyPresent(*present);
        present->Runtime   = runtime;
        present->TimeTaken = *(uint64_t*) &hdr.TimeStamp - present->QpcTime;

        if (AllowPresentBatching && mTrackDisplay) {
            // We now remove this present from mPresentByThreadId because any future
            // event related to it (e.g., from DXGK/Win32K/etc.) is not expected to
            // come from this thread.
            mPresentByThreadId.erase(eventIter);
        } else {
            present->FinalState = AllowPresentBatching ? PresentResult::Presented : PresentResult::Discarded;
            CompletePresent(present);
        }
    }

    // Complete any deferred completions whose present wait count has now
    // expired.  All tracking has already been removed, so we only need to add
    // these to the completed list.
    //
    // Even if deferredCompletions->mPresents becomes empty, we leave the
    // mDeferredCompletions entry because we're likely to keep using it for
    // this process.
    auto deferredIter = mDeferredCompletions.find(hdr.ProcessId);
    if (deferredIter != mDeferredCompletions.end()) {
        auto deferredCompletions = &deferredIter->second;
        for (auto ii = deferredCompletions->begin(); ii != deferredCompletions->end(); ) {
            auto waitCount = &ii->second;
            if (*waitCount == 1) {
                CompleteDeferredCompletion(ii->first);
                ii = deferredCompletions->erase(ii);
            } else {
                --*waitCount;
                ++ii;
            }
        }
    }
}

void PMTraceConsumer::HandleNTProcessEvent(EVENT_RECORD* pEventRecord)
{
    if (pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_START ||
        pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_DC_START ||
        pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_END||
        pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_DC_END) {
        EventDataDesc desc[] = {
            { L"ProcessId" },
            { L"ImageFileName" },
        };
        mMetadata.GetEventData(pEventRecord, desc, _countof(desc));

        ProcessEvent event;
        event.QpcTime       = pEventRecord->EventHeader.TimeStamp.QuadPart;
        event.ProcessId     = desc[0].GetData<uint32_t>();
        event.ImageFileName = desc[1].GetData<std::string>();
        event.IsStartEvent  = pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_START ||
                              pEventRecord->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_DC_START;

        std::lock_guard<std::mutex> lock(mProcessEventMutex);
        mProcessEvents.emplace_back(event);
        return;
    }
}

void PMTraceConsumer::HandleMetadataEvent(EVENT_RECORD* pEventRecord)
{
    mMetadata.AddMetadata(pEventRecord);
}

void PMTraceConsumer::AddTrackedProcessForFiltering(uint32_t processID)
{
    std::unique_lock<std::shared_mutex> lock(mTrackedProcessFilterMutex);
    mTrackedProcessFilter.insert(processID);
}

void PMTraceConsumer::RemoveTrackedProcessForFiltering(uint32_t processID)
{
    std::unique_lock<std::shared_mutex> lock(mTrackedProcessFilterMutex);
    auto iterator = mTrackedProcessFilter.find(processID);
    
    if (iterator != mTrackedProcessFilter.end()) {
        mTrackedProcessFilter.erase(processID);
    }
    else {
        assert(false);
    }

    // Completion events will remove any currently tracked events for this process
    // from data structures, so we don't need to proactively remove them now.
}

bool PMTraceConsumer::IsProcessTrackedForFiltering(uint32_t processID)
{
    if (!mFilteredProcessIds || processID == DwmProcessId) {
        return true;
    }

    std::shared_lock<std::shared_mutex> lock(mTrackedProcessFilterMutex);
    auto iterator = mTrackedProcessFilter.find(processID);
    return (iterator != mTrackedProcessFilter.end());
}

#ifdef TRACK_PRESENT_PATHS
static_assert(__COUNTER__ <= 64, "Too many TRACK_PRESENT ids to store in PresentEvent::AnalysisPath");
#endif
