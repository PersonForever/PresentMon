#include "../IntelPresentMon/CommonUtilities/win/WinAPI.h"
#include "../IntelPresentMon/CommonUtilities/Hash.h"
#include <initguid.h>
#include <evntcons.h>
#include <cguid.h>
#include <atlbase.h>
#include <comdef.h>
#include <objbase.h>
#include <relogger.h>
#include <iostream>
#include <optional>
#include <format>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "CliOptions.h"
#include "../PresentData/PresentMonTraceSession.hpp"
#include "../PresentData/PresentMonTraceConsumer.hpp"
#include "../PresentData/ETW/Microsoft_Windows_EventMetadata.h"
#include "../PresentData/ETW/NT_Process.h"

DEFINE_GUID(IID_ITraceRelogger, 0xF754AD43, 0x3BCC, 0x4286, 0x80, 0x09, 0x9C, 0x5D, 0xA2, 0x14, 0xE8, 0x4E); // {F754AD43-3BCC-4286-8009-9C5DA214E84E}
DEFINE_GUID(IID_ITraceEventCallback, 0x3ED25501, 0x593F, 0x43E9, 0x8F, 0x38, 0x3A, 0xB4, 0x6F, 0x5A, 0x4A, 0x52); // {3ED25501-593F-43E9-8F38-3AB46F5A4A52}

enum class Mode
{
    Analysis,
    Trim,
};

struct ProviderFilter
{
    std::unordered_set<uint16_t> eventSet;
    uint64_t anyKeyMask;
    uint64_t allKeyMask;
    uint8_t maxLevel;
    GUID providerGuid;
};

namespace std
{
    template <>
    struct hash<GUID>
    {
        size_t operator()(const GUID& guid) const noexcept
        {
            return pmon::util::hash::HashGuid(guid);
        }
    };
}

class Filter : public IFilterBuildListener
{
public:
    Filter()
    {
        // there are events from providers that are not explicitly enabled / filtered by presentmon
        // but that are nonetheless required for PresentMon tracking operations
        // they are likely enabled by default when doing ETW tracing
        // we must add them manually here instead of adding them by listening to the FilteredProvider

        // NT_Process
        //
        ProviderEnabled(NT_Process::GUID, 0, 0, 100);

        // Microsoft_Windows_EventMetadata::GUID
        //
        ProviderEnabled(Microsoft_Windows_EventMetadata::GUID, 0, 0, 100);
    }
    // Inherited via IFilterBuildListener
    void EventAdded(uint16_t id) override
    {
        eventsOnDeck_.push_back(id);
    }
    void ProviderEnabled(const GUID& providerGuid, uint64_t anyKey, uint64_t allKey, uint8_t maxLevel) override
    {
        ProviderFilter filter{
            .anyKeyMask = anyKey ? anyKey : 0xFFFF'FFFF,
            .allKeyMask = allKey,
            .maxLevel = maxLevel,
            .providerGuid = providerGuid,
        };
        filter.eventSet.insert_range(eventsOnDeck_);
        ClearEvents();
        providers_.emplace(filter.providerGuid, filter);
    }
    void ClearEvents() override
    {
        eventsOnDeck_.clear();
    }
    const ProviderFilter* LookupProvider(const GUID& guid)
    {
        if (auto i = providers_.find({ guid }); i != providers_.end()) {
            return &i->second;
        }
        return nullptr;
    }
private:
    std::vector<uint16_t> eventsOnDeck_;
    std::unordered_map<GUID, ProviderFilter> providers_;
};

class EventCallback : public ITraceEventCallback
{
private:
    // COM support
    DWORD ref_count_ = 0;
    // operation mode
    Mode mode_;
    // trim region
    std::optional<std::pair<uint64_t, uint64_t>> trimRange_;
    // pruning options
    std::shared_ptr<Filter> pFilter_;
    bool byId_;
    bool byKeyword_;
    bool byLevel_;
    // analysis stats
    int eventCount_ = 0;
    int keepCount_ = 0;
    std::optional<uint64_t> firstTimestamp_;
    uint64_t lastTimestamp_ = 0;

public:
    EventCallback(bool infoOnly, std::shared_ptr<Filter> pFilter, bool byId, bool byKeyword, bool byLevel)
        :
        mode_{ infoOnly ? Mode::Analysis : Mode::Trim },
        pFilter_{ std::move(pFilter) },
        byId_{ byId },
        byKeyword_{ byKeyword },
        byLevel_{ byLevel }
    {}
    STDMETHODIMP QueryInterface(const IID& iid, void** pObj)
    {
        if (iid == IID_IUnknown) {
            *pObj = dynamic_cast<IUnknown*>(this);
        }
        else if (iid == IID_ITraceEventCallback) {
            *pObj = dynamic_cast<ITraceEventCallback*>(this);
        }
        else {
            *pObj = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef(void)
    {
        return InterlockedIncrement(&ref_count_);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG ucount = InterlockedDecrement(&ref_count_);
        if (ucount == 0) {
            delete this;
        }
        return ucount;
    }
    HRESULT STDMETHODCALLTYPE OnBeginProcessTrace(ITraceEvent* pHeaderEvent, ITraceRelogger* pRelogger)
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(ITraceEvent* pEvent, ITraceRelogger* pRelogger)
    {
        EVENT_RECORD* pEvt = nullptr;
        pEvent->GetEventRecord(&pEvt);
        const auto& hdr = pEvt->EventHeader;
        const auto& desc = hdr.EventDescriptor;
        const auto ts = (uint64_t)hdr.TimeStamp.QuadPart;
        if (!firstTimestamp_) {
            firstTimestamp_ = ts;
        }
        lastTimestamp_ = ts;
        eventCount_++;
        if (trimRange_) {
            if (ts < trimRange_->first || ts > trimRange_->second) {
                return S_OK;
            }
        }
        // filter only if we have a filter object
        if (pFilter_) {
            if (auto pProducerFilter = pFilter_->LookupProvider(hdr.ProviderId)) {
                // if filtering by event id, discard if id not in set
                // empty set indicates this provider should not be filtered by id
                if (byId_ && !pProducerFilter->eventSet.empty() && !pProducerFilter->eventSet.contains(desc.Id)) {
                    return S_OK;
                }
                // if keyword filtering, discard if keyword contains no bits from any mask,
                // or is missing 1 or more bits from all mask
                if (byKeyword_) {
                    const bool all = (pProducerFilter->allKeyMask & desc.Keyword) == pProducerFilter->allKeyMask;
                    const bool any = !bool(pProducerFilter->anyKeyMask) || bool(pProducerFilter->anyKeyMask & desc.Keyword);
                    if (!all || !any) {
                        return S_OK;
                    }
                }
                // level filter is simple threshold
                if (byLevel_ && desc.Level > pProducerFilter->maxLevel) {
                    return S_OK;
                }
            }
            else {
                return S_OK;
            }
        }
        keepCount_++;
        if (mode_ == Mode::Trim) {
            pRelogger->Inject(pEvent);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFinalizeProcessTrace(ITraceRelogger* pRelogger)
    {
        return S_OK;
    }
    int GetEventCount() const
    {
        return eventCount_;
    }
    std::pair<uint64_t, uint64_t> GetTimestampRange() const
    {
        return { firstTimestamp_.value_or(0), lastTimestamp_};
    }
    void SetTrimRange(std::pair<uint64_t, uint64_t> range)
    {
        trimRange_ = range;
    }
    int GetKeepCount() const
    {
        return keepCount_;
    }
};

class TempFile
{
public:
    operator const CComBSTR& () const
    {
        return name_;
    }
    ~TempFile()
    {
        std::filesystem::remove((const wchar_t*)name_);
    }
private:
    CComBSTR name_ = "null-log.etl.tmp";
};


int main(int argc, const char** argv)
{
    // parse command line, return with error code from CLI11 if running as app
    if (auto e = clio::Options::Init(argc, argv)) {
        return *e;
    }
    auto& opt = clio::Options::Get();

    if (opt.trimRange) {
        auto& r = *opt.trimRange;
        if (r.first > r.second) {
            std::cout << "Lower bound of trim range [" << r.first << "] must not exceed upper [" << r.second << "]" << std::endl;
            return -1;
        }
    }

    std::locale::global(std::locale("en_US.UTF-8"));

    if (auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); FAILED(hr)) {
        std::cout << "Failed to init COM" << std::endl;
        return -1;
    }

    ITraceRelogger* pRelogger = nullptr;
    if (auto hr = CoCreateInstance(CLSID_TraceRelogger, 0, CLSCTX_INPROC_SERVER, IID_ITraceRelogger, reinterpret_cast<void**>(&pRelogger));
        FAILED(hr)) {
        std::cout << "Failed to create trace relogger instance" << std::endl;
        return -1;
    }

    TRACEHANDLE relogTraceHandle;
    const CComBSTR inputEtlName = (*opt.inputFile).c_str();
    if (auto hr = pRelogger->AddLogfileTraceStream(inputEtlName, nullptr, &relogTraceHandle); FAILED(hr)) {
        std::cout << "Failed to add logfile: " << *opt.inputFile << std::endl;
    }

    // we must output to etl file no matter what
    // create a dummy temp etl if user did not specify output
    CComBSTR outputEtlName;
    std::optional<TempFile> temp;
    if (opt.outputFile) {
        outputEtlName = (*opt.outputFile).c_str();
    }
    else {
        temp.emplace();
        outputEtlName = *temp;
    }
    if (auto hr = pRelogger->SetOutputFilename(outputEtlName); FAILED(hr)) {
        std::cout << "Failed to set output file: " << *opt.outputFile << std::endl;
    }

    // do a dry run of PresentMon provider/filter processing to enumerate the filter parameters
    std::shared_ptr<Filter> pFilter;
    if (opt.provider) {
        pFilter = std::make_shared<Filter>();
        PMTraceConsumer traceConsumer;
        traceConsumer.mTrackDisplay = true;   // ... presents to the display.
        traceConsumer.mTrackGPU = true;       // ... GPU work.
        traceConsumer.mTrackGPUVideo = true;  // ... GPU video work (separately from non-video GPU work).
        traceConsumer.mTrackInput = true;     // ... keyboard/mouse latency.
        traceConsumer.mTrackFrameType = true; // ... the frame type communicated through the Intel-PresentMon provider.
        EnableProvidersListing(0, nullptr, &traceConsumer, true, true, pFilter);
    }

    auto pCallbackProcessor = std::make_unique<EventCallback>(!opt.outputFile, pFilter,
        (bool)opt.event, (bool)opt.keyword, (bool)opt.level);
    if (auto hr = pRelogger->RegisterCallback(pCallbackProcessor.get()); FAILED(hr)) {
        std::cout << "Failed to register callback" << std::endl;
    }

    if (opt.trimRange) {
        pCallbackProcessor->SetTrimRange(*opt.trimRange);
    }

    if (auto hr = pRelogger->ProcessTrace(); FAILED(hr)) {
        std::cout << "Failed to process trace" << std::endl;
    }

    const auto tsr = pCallbackProcessor->GetTimestampRange();
    const auto dur = tsr.second - tsr.first;
    std::cout << std::format(" ======== Report for [ {} ] ========\n", *opt.inputFile);
    std::cout << std::format("Total event count: {:L}\n", pCallbackProcessor->GetEventCount());
    std::cout << std::format("Timestamp range {:L} - {:L} (duration: {:L})\n", tsr.first, tsr.second, dur);
    std::cout << std::format("Duration of trace in milliseconds: {:L}\n\n", double(dur) / 10'000.);

    std::cout << std::format("Events trimmed and/or filtered: {:L}\n",
        pCallbackProcessor->GetEventCount() - pCallbackProcessor->GetKeepCount());
    std::cout << std::format("Count of persisted events: {:L}\n", pCallbackProcessor->GetKeepCount());

    if (!opt.outputFile) {
        std::cout << "No output specified; running in analysis mode\n";
    }
    else {
        std::cout << "Output written to: " << *opt.outputFile << std::endl;
    }

    return 0;
}

namespace pmon::util::log
{
    std::shared_ptr<class IChannel> GetDefaultChannel() noexcept
    {
        return {};
    }
}