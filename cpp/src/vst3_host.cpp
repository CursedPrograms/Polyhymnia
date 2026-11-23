// Polyhymnia - VST3 plugin hosting (after SoundCraft crates/vst3-host).
#include "vst3_host.h"
#include "app.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/vstspeaker.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vsttypes.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

// ─── Steinberg Interface IID Definitions ─────────────────────────────────────
namespace Steinberg {

DEF_CLASS_IID(FUnknown)
DEF_CLASS_IID(IPluginBase)
DEF_CLASS_IID(IPluginFactory)
DEF_CLASS_IID(IPluginFactory2)
DEF_CLASS_IID(IPluginFactory3)
DEF_CLASS_IID(IBStream)
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)

namespace Vst {

DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IMidiMapping)

} // namespace Vst
} // namespace Steinberg

namespace Vst3Host {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr size_t kEventCap = 1024;
constexpr size_t kParamQueueCap = 1024;
constexpr size_t kEditCap = 256;
constexpr const char* kStateMagic = "SCV3";

std::string readC16(const TChar* s) {
    if (!s) return {};
    std::string out;
    while (*s) {
        uint16_t u = static_cast<uint16_t>(*s++);
        if (u < 0x80) {
            out.push_back(static_cast<char>(u));
        } else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return out;
}

void writeC16(const std::string& s, TChar* dst, size_t maxLen = 128) {
    if (!dst || maxLen == 0) return;
    size_t i = 0, di = 0;
    while (i < s.size() && di + 1 < maxLen) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            dst[di++] = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            dst[di++] = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            dst[di++] = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            i += 3;
        } else {
            i += 1;
        }
    }
    dst[di] = 0;
}

std::string readC8(const char8* s) {
    if (!s) return {};
    return std::string(reinterpret_cast<const char*>(s));
}

bool tuidMatches(const TUID a, const TUID b) {
    return std::memcmp(a, b, sizeof(TUID)) == 0;
}

} // namespace

std::string toHex(const uint8_t cid[16]) {
    std::ostringstream ss;
    for (int i = 0; i < 16; ++i) {
        ss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(cid[i]);
    }
    return ss.str();
}

std::optional<std::array<uint8_t, 16>> parseId(const std::string& id) {
    const std::string prefix = kIdPrefix;
    if (id.rfind(prefix, 0) != 0) return std::nullopt;
    const std::string hexStr = id.substr(prefix.size());
    if (hexStr.size() != 32) return std::nullopt;
    std::array<uint8_t, 16> out{};
    for (size_t i = 0; i < 16; ++i) {
        char h1 = hexStr[i * 2];
        char h2 = hexStr[i * 2 + 1];
        if (!std::isxdigit(static_cast<unsigned char>(h1)) || !std::isxdigit(static_cast<unsigned char>(h2))) {
            return std::nullopt;
        }
        auto val = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[i] = (val(h1) << 4) | val(h2);
    }
    return out;
}

std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> splitState(const uint8_t* data, size_t size) {
    if (!data || size == 0) return std::nullopt;
    if (size > kMaxStateBytes * 2) return std::nullopt;

    if (size >= 4 && std::memcmp(data, kStateMagic, 4) == 0) {
        size_t offset = 4;
        if (size < offset + 4) return std::nullopt;
        uint32_t compLen = 0;
        std::memcpy(&compLen, data + offset, 4);
        offset += 4;
        if (compLen > kMaxStateBytes || size < offset + compLen + 4) return std::nullopt;
        std::vector<uint8_t> comp(data + offset, data + offset + compLen);
        offset += compLen;

        uint32_t ctrlLen = 0;
        std::memcpy(&ctrlLen, data + offset, 4);
        offset += 4;
        if (ctrlLen > kMaxStateBytes || offset + ctrlLen != size) return std::nullopt;
        std::vector<uint8_t> ctrl(data + offset, data + offset + ctrlLen);
        return std::make_pair(std::move(comp), std::move(ctrl));
    }

    // Bare component state without SoundCraft container header
    std::vector<uint8_t> comp(data, data + size);
    return std::make_pair(std::move(comp), std::vector<uint8_t>{});
}

std::vector<uint8_t> makeStateBlob(const std::vector<uint8_t>& compState, const std::vector<uint8_t>& ctrlState) {
    std::vector<uint8_t> out;
    out.reserve(4 + 4 + compState.size() + 4 + ctrlState.size());
    out.insert(out.end(), kStateMagic, kStateMagic + 4);
    uint32_t compLen = static_cast<uint32_t>(compState.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&compLen), reinterpret_cast<const uint8_t*>(&compLen) + 4);
    out.insert(out.end(), compState.begin(), compState.end());
    uint32_t ctrlLen = static_cast<uint32_t>(ctrlState.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&ctrlLen), reinterpret_cast<const uint8_t*>(&ctrlLen) + 4);
    out.insert(out.end(), ctrlState.begin(), ctrlState.end());
    return out;
}

// ─── MemoryStream (implements IBStream) ───────────────────────────────────────
class MemoryStream : public IBStream {
public:
    explicit MemoryStream(std::vector<uint8_t> data = {})
        : data_(std::move(data)), cursor_(0), refCount_(1) {}

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IBStream::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IBStream*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        if (!buffer || numBytes < 0) return kInvalidArgument;
        int32 available = static_cast<int32>(std::max<int64_t>(0, static_cast<int64_t>(data_.size()) - cursor_));
        int32 toRead = std::min(numBytes, available);
        if (toRead > 0) {
            std::memcpy(buffer, data_.data() + cursor_, toRead);
            cursor_ += toRead;
        }
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (!buffer || numBytes < 0) return kInvalidArgument;
        size_t required = static_cast<size_t>(cursor_ + numBytes);
        if (required > data_.size()) data_.resize(required);
        std::memcpy(data_.data() + cursor_, buffer, numBytes);
        cursor_ += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
        int64_t newPos = cursor_;
        if (mode == kIBSeekSet) newPos = pos;
        else if (mode == kIBSeekCur) newPos += pos;
        else if (mode == kIBSeekEnd) newPos = static_cast<int64_t>(data_.size()) + pos;
        else return kInvalidArgument;

        if (newPos < 0) return kInvalidArgument;
        cursor_ = newPos;
        if (result) *result = cursor_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* result) override {
        if (!result) return kInvalidArgument;
        *result = cursor_;
        return kResultOk;
    }

    const std::vector<uint8_t>& data() const { return data_; }
    void rewind() { cursor_ = 0; }

private:
    std::vector<uint8_t> data_;
    int64_t cursor_;
    std::atomic<uint32_t> refCount_;
};

// ─── HostAttributes (implements IAttributeList) ──────────────────────────────
class HostAttributes : public IAttributeList {
public:
    HostAttributes() : refCount_(1) {}

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IAttributeList::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IAttributeList*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    tresult PLUGIN_API setInt(AttrID id, int64 value) override {
        if (!id) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        ints_[id] = value;
        return kResultOk;
    }

    tresult PLUGIN_API getInt(AttrID id, int64& value) override {
        if (!id) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = ints_.find(id);
        if (it != ints_.end()) { value = it->second; return kResultOk; }
        return kResultFalse;
    }

    tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (!id) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        floats_[id] = value;
        return kResultOk;
    }

    tresult PLUGIN_API getFloat(AttrID id, double& value) override {
        if (!id) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = floats_.find(id);
        if (it != floats_.end()) { value = it->second; return kResultOk; }
        return kResultFalse;
    }

    tresult PLUGIN_API setString(AttrID id, const TChar* string) override {
        if (!id || !string) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<TChar> s;
        while (*string) s.push_back(*string++);
        s.push_back(0);
        strings_[id] = s;
        return kResultOk;
    }

    tresult PLUGIN_API getString(AttrID id, TChar* string, uint32 sizeInBytes) override {
        if (!id || !string || sizeInBytes < sizeof(TChar)) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = strings_.find(id);
        if (it != strings_.end()) {
            size_t maxChars = (sizeInBytes / sizeof(TChar)) - 1;
            size_t toCopy = std::min(maxChars, it->second.size() > 0 ? it->second.size() - 1 : 0);
            for (size_t i = 0; i < toCopy; ++i) string[i] = it->second[i];
            string[toCopy] = 0;
            return kResultOk;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 sizeInBytes) override {
        if (!id || (!data && sizeInBytes > 0)) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        blobs_[id] = std::vector<uint8_t>(p, p + sizeInBytes);
        return kResultOk;
    }

    tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& sizeInBytes) override {
        if (!id) return kInvalidArgument;
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = blobs_.find(id);
        if (it != blobs_.end()) {
            data = it->second.data();
            sizeInBytes = static_cast<uint32>(it->second.size());
            return kResultOk;
        }
        data = nullptr;
        sizeInBytes = 0;
        return kResultFalse;
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, int64> ints_;
    std::unordered_map<std::string, double> floats_;
    std::unordered_map<std::string, std::vector<TChar>> strings_;
    std::unordered_map<std::string, std::vector<uint8_t>> blobs_;
    std::atomic<uint32_t> refCount_;
};

// ─── HostMessage (implements IMessage) ─────────────────────────────────────────
class HostMessage : public IMessage {
public:
    HostMessage() : attrs_(new HostAttributes()), refCount_(1) {}
    ~HostMessage() { attrs_->release(); }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IMessage::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IMessage*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    FIDString PLUGIN_API getMessageID() override { return msgId_.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { msgId_ = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { attrs_->addRef(); return attrs_; }

private:
    std::string msgId_;
    HostAttributes* attrs_;
    std::atomic<uint32_t> refCount_;
};

// ─── HostApplication (implements IHostApplication & IComponentHandler) ─────────
class HostApplication : public IHostApplication, public IComponentHandler {
public:
    HostApplication() : refCount_(1) {}

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IHostApplication::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IHostApplication*>(this);
            return kResultOk;
        }
        if (tuidMatches(_iid, IComponentHandler::iid)) {
            addRef();
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    // IHostApplication
    tresult PLUGIN_API getName(String128 name) override {
        writeC16("Polyhymnia", name);
        return kResultOk;
    }

    tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (tuidMatches(cid, IMessage::iid) && (tuidMatches(_iid, IMessage::iid) || tuidMatches(_iid, FUnknown::iid))) {
            auto* msg = new HostMessage();
            *obj = static_cast<IMessage*>(msg);
            return kResultOk;
        }
        if (tuidMatches(cid, IAttributeList::iid) && (tuidMatches(_iid, IAttributeList::iid) || tuidMatches(_iid, FUnknown::iid))) {
            auto* attrs = new HostAttributes();
            *obj = static_cast<IAttributeList*>(attrs);
            return kResultOk;
        }
        return kResultFalse;
    }

    // IComponentHandler
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) override {
        if (std::isfinite(valueNormalized)) {
            std::lock_guard<std::mutex> lk(editsMtx_);
            const double v = std::clamp(valueNormalized, 0.0, 1.0);
            for (auto& item : edits_) {
                if (item.first == id) {
                    item.second = v;
                    return kResultOk;
                }
            }
            if (edits_.size() < kEditCap) {
                edits_.emplace_back(id, v);
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    std::vector<std::pair<uint32_t, double>> takeEdits() {
        std::lock_guard<std::mutex> lk(editsMtx_);
        auto copy = edits_;
        edits_.clear();
        return copy;
    }

private:
    std::mutex editsMtx_;
    std::vector<std::pair<uint32_t, double>> edits_;
    std::atomic<uint32_t> refCount_;
};

// ─── ParamQueue & ParamChanges (implements IParameterChanges) ──────────────────
class ParamQueue : public IParamValueQueue {
public:
    ParamQueue() : paramId_(0), refCount_(1) {}

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IParamValueQueue::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IParamValueQueue*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    ParamID PLUGIN_API getParameterId() override { return paramId_; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points_.size()); }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override {
        if (index < 0 || index >= static_cast<int32>(points_.size())) return kInvalidArgument;
        sampleOffset = points_[index].first;
        value = points_[index].second;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue value, int32& index) override {
        points_.emplace_back(sampleOffset, value);
        index = static_cast<int32>(points_.size() - 1);
        return kResultOk;
    }

    void set(ParamID id, int32 sampleOffset, ParamValue value) {
        paramId_ = id;
        points_.clear();
        points_.emplace_back(sampleOffset, value);
    }
    void clear() { points_.clear(); }

private:
    ParamID paramId_;
    std::vector<std::pair<int32, ParamValue>> points_;
    std::atomic<uint32_t> refCount_;
};

class ParamChanges : public IParameterChanges {
public:
    explicit ParamChanges(size_t capacity = kParamQueueCap) : capacity_(capacity), activeCount_(0), refCount_(1) {
        for (size_t i = 0; i < capacity_; ++i) queues_.push_back(new ParamQueue());
    }
    ~ParamChanges() {
        for (auto* q : queues_) q->release();
    }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IParameterChanges::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IParameterChanges*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(activeCount_); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        if (index < 0 || index >= static_cast<int32>(activeCount_)) return nullptr;
        queues_[index]->addRef();
        return queues_[index];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (size_t i = 0; i < activeCount_; ++i) {
            if (queues_[i]->getParameterId() == id) {
                index = static_cast<int32>(i);
                queues_[i]->addRef();
                return queues_[i];
            }
        }
        if (activeCount_ >= capacity_) {
            index = -1;
            return nullptr;
        }
        index = static_cast<int32>(activeCount_++);
        queues_[index]->set(id, 0, 0.0);
        queues_[index]->addRef();
        return queues_[index];
    }

    void set(ParamID id, int32 sampleOffset, ParamValue value) {
        for (size_t i = 0; i < activeCount_; ++i) {
            if (queues_[i]->getParameterId() == id) {
                queues_[i]->set(id, sampleOffset, value);
                return;
            }
        }
        if (activeCount_ < capacity_) {
            queues_[activeCount_]->set(id, sampleOffset, value);
            ++activeCount_;
        }
    }
    void clear() { activeCount_ = 0; }

    const std::vector<ParamQueue*>& queues() const { return queues_; }

private:
    size_t capacity_;
    size_t activeCount_;
    std::vector<ParamQueue*> queues_;
    std::atomic<uint32_t> refCount_;
};

// ─── EventList (implements IEventList) ────────────────────────────────────────
class EventList : public IEventList {
public:
    explicit EventList(size_t capacity = kEventCap) : capacity_(capacity), refCount_(1) {}

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IEventList::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IEventList*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kInvalidArgument;
        e = events_[index];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        if (events_.size() >= capacity_) return kResultFalse;
        events_.push_back(e);
        return kResultOk;
    }

    void clear() { events_.clear(); }

private:
    size_t capacity_;
    std::vector<Event> events_;
    std::atomic<uint32_t> refCount_;
};

// ─── VST3 Editor Window (implements IPlugFrame) ──────────────────────────────
#ifdef _WIN32

static const wchar_t* kEditorClassName = L"PolyhymniaVst3Editor";

LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void registerEditorClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = editorWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kEditorClassName;
    RegisterClassExW(&wc);
    registered = true;
}

#endif

class Vst3Editor : public IPlugFrame {
public:
    Vst3Editor(IPlugView* view, HostApplication* host, std::string title)
        : view_(view), host_(host), title_(std::move(title)), hwnd_(nullptr), refCount_(1) {
        if (view_) view_->addRef();
        if (host_) host_->addRef();
    }

    ~Vst3Editor() {
        close();
        if (view_) { view_->release(); view_ = nullptr; }
        if (host_) { host_->release(); host_ = nullptr; }
    }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (tuidMatches(_iid, IPlugFrame::iid) || tuidMatches(_iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IPlugFrame*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount_; }
    uint32 PLUGIN_API release() override {
        uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize) return kInvalidArgument;
#ifdef _WIN32
        if (hwnd_) {
            const int w = std::clamp(newSize->right - newSize->left, 100, 4096);
            const int h = std::clamp(newSize->bottom - newSize->top, 100, 4096);
            RECT wr = {0, 0, w, h};
            AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, FALSE, 0);
            SetWindowPos(hwnd_, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            view->onSize(newSize);
        }
#endif
        return kResultOk;
    }

    bool open() {
        if (!view_) return false;
#ifdef _WIN32
        if (hwnd_ && IsWindow(hwnd_)) {
            ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            return true;
        }
        registerEditorClass();

        ViewRect r{0, 0, 0, 0};
        if (view_->getSize(&r) != kResultOk) {
            r = {0, 0, 600, 400};
        }
        const int w = std::clamp(r.right - r.left, 100, 4096);
        const int h = std::clamp(r.bottom - r.top, 100, 4096);

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        RECT wr = {0, 0, w, h};
        AdjustWindowRectEx(&wr, style, FALSE, 0);

        HWND owner = GetActiveWindow();
        std::wstring wtitle(title_.begin(), title_.end());
        hwnd_ = CreateWindowExW(0, kEditorClassName, wtitle.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) return false;

        view_->setFrame(this);
        if (view_->attached(reinterpret_cast<void*>(hwnd_), kPlatformTypeHWND) != kResultOk) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            return false;
        }
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
#else
        return false;
#endif
    }

    void close() {
#ifdef _WIN32
        if (hwnd_) {
            if (view_) view_->removed();
            if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
#endif
    }

    bool isOpen() const {
#ifdef _WIN32
        return hwnd_ && IsWindow(hwnd_) && IsWindowVisible(hwnd_);
#else
        return false;
#endif
    }

    std::vector<std::pair<uint32_t, double>> idle() {
        if (!isOpen() || !host_) return {};
        return host_->takeEdits();
    }

private:
    IPlugView* view_;
    HostApplication* host_;
    std::string title_;
#ifdef _WIN32
    HWND hwnd_;
#else
    void* hwnd_;
#endif
    std::atomic<uint32_t> refCount_;
};

// ─── Vst3Plugin (implements Dsp::Plugin) ───────────────────────────────────────
class Vst3Plugin : public Dsp::Plugin {
public:
    Vst3Plugin(HMODULE mod, IComponent* comp, IAudioProcessor* proc, IEditController* ctrl,
               Vst3Descriptor desc, std::vector<Dsp::ParamInfo> pInfos)
        : module_(mod), comp_(comp), proc_(proc), ctrl_(ctrl),
          desc_(std::move(desc)), paramInfos_(std::move(pInfos)),
          hostApp_(new HostApplication()), inParams_(new ParamChanges()), outParams_(new ParamChanges()),
          inEvents_(new EventList()), outEvents_(new EventList()),
          editor_(nullptr), sampleRate_(48000.0f), maxBlock_(512), channels_(2),
          latency_(0), tail_(0), offline_(false) {
        comp_->addRef();
        if (proc_) proc_->addRef();
        if (ctrl_) ctrl_->addRef();

        info_.id = desc_.id.c_str();
        info_.name = desc_.name.c_str();
        info_.shortName = desc_.name.size() > 8 ? desc_.name.substr(0, 8).c_str() : desc_.name.c_str();
        info_.category = desc_.category;
        info_.params = paramInfos_;
        info_.isInstrument = desc_.isInstrument;
        info_.audiosuite = true;

        if (proc_) {
            latency_ = proc_->getLatencySamples();
            tail_ = proc_->getTailSamples();
        }
    }

    ~Vst3Plugin() override {
        if (editor_) {
            editor_->close();
            editor_->release();
            editor_ = nullptr;
        }
        if (proc_) {
            proc_->setProcessing(false);
            proc_->release();
            proc_ = nullptr;
        }
        if (comp_) {
            comp_->setActive(false);
            comp_->terminate();
            comp_->release();
            comp_ = nullptr;
        }
        if (ctrl_) {
            ctrl_->terminate();
            ctrl_->release();
            ctrl_ = nullptr;
        }
        hostApp_->release();
        inParams_->release();
        outParams_->release();
        inEvents_->release();
        outEvents_->release();

        // Note: module_ is kept loaded for process lifetime per SoundCraft architecture.
    }

    const Dsp::PluginInfo& info() const override { return info_; }

    void prepare(float sampleRate, size_t maxBlock, size_t channels) override {
        sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0f;
        maxBlock_ = maxBlock > 0 ? maxBlock : 512;
        channels_ = std::max<size_t>(1, channels);

        if (proc_) {
            proc_->setProcessing(false);
            comp_->setActive(false);

            SpeakerArrangement inArr = (channels_ == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
            SpeakerArrangement outArr = (channels_ == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
            proc_->setBusArrangements(&inArr, 1, &outArr, 1);

            ProcessSetup setup{};
            setup.processMode = offline_ ? kOffline : kRealtime;
            setup.symbolicSampleSize = kSample32;
            setup.maxSamplesPerBlock = static_cast<int32>(maxBlock_);
            setup.sampleRate = sampleRate_;
            proc_->setupProcessing(setup);

            comp_->activateBus(kAudio, kInput, 0, true);
            comp_->activateBus(kAudio, kOutput, 0, true);
            comp_->setActive(true);
            proc_->setProcessing(true);

            latency_ = proc_->getLatencySamples();
            tail_ = proc_->getTailSamples();
        }
    }

    void reset() override {
        if (proc_) {
            proc_->setProcessing(false);
            proc_->setProcessing(true);
        }
    }

    bool setParam(const std::string& id, float value) override {
        if (!ctrl_) return false;
        try {
            ParamID pid = static_cast<ParamID>(std::stoul(id));
            const auto* p = info_.param(id);
            const float norm = p ? p->toNormalized(value) : std::clamp(value, 0.0f, 1.0f);
            ctrl_->setParamNormalized(pid, norm);
            inParams_->set(pid, 0, norm);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::optional<float> param(const std::string& id) const override {
        if (!ctrl_) return std::nullopt;
        try {
            ParamID pid = static_cast<ParamID>(std::stoul(id));
            ParamValue norm = ctrl_->getParamNormalized(pid);
            const auto* p = info_.param(id);
            return p ? p->fromNormalized(static_cast<float>(norm)) : static_cast<float>(norm);
        } catch (...) {
            return std::nullopt;
        }
    }

    void process(std::vector<std::vector<float>>& io, size_t frames) override {
        if (!proc_ || io.empty() || frames == 0) return;
        const size_t numCh = io.size();

        std::vector<float*> channelPointers(numCh);
        for (size_t c = 0; c < numCh; ++c) {
            if (io[c].size() < frames) io[c].resize(frames, 0.0f);
            channelPointers[c] = io[c].data();
        }

        AudioBusBuffers inBus{};
        inBus.numChannels = static_cast<int32>(numCh);
        inBus.channelBuffers32 = channelPointers.data();

        AudioBusBuffers outBus{};
        outBus.numChannels = static_cast<int32>(numCh);
        outBus.channelBuffers32 = channelPointers.data();

        ProcessData data{};
        data.processMode = offline_ ? kOffline : kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(frames);
        data.numInputs = 1;
        data.inputs = &inBus;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = inParams_;
        data.outputParameterChanges = outParams_;
        data.inputEvents = inEvents_;
        data.outputEvents = outEvents_;

        proc_->process(data);

        inParams_->clear();
        inEvents_->clear();
    }

    size_t latency() const override { return latency_; }
    size_t tailSamples() const override { return tail_; }

    std::vector<uint8_t> saveState() {
        std::vector<uint8_t> compBytes;
        if (comp_) {
            auto* s = new MemoryStream();
            if (comp_->getState(s) == kResultOk) compBytes = s->data();
            s->release();
        }
        std::vector<uint8_t> ctrlBytes;
        if (ctrl_) {
            auto* s = new MemoryStream();
            if (ctrl_->getState(s) == kResultOk) ctrlBytes = s->data();
            s->release();
        }
        return makeStateBlob(compBytes, ctrlBytes);
    }

    bool loadState(const std::vector<uint8_t>& data) {
        auto parts = splitState(data.data(), data.size());
        if (!parts.has_value()) return false;
        if (comp_ && !parts->first.empty()) {
            auto* s = new MemoryStream(parts->first);
            comp_->setState(s);
            s->release();
        }
        if (ctrl_) {
            if (!parts->first.empty()) {
                auto* s = new MemoryStream(parts->first);
                ctrl_->setComponentState(s);
                s->release();
            }
            if (!parts->second.empty()) {
                auto* s = new MemoryStream(parts->second);
                ctrl_->setState(s);
                s->release();
            }
        }
        return true;
    }

    bool openEditor() {
        if (!ctrl_) return false;
        if (!editor_) {
            IPlugView* view = ctrl_->createView(ViewType::kEditor);
            if (!view) return false;
            editor_ = new Vst3Editor(view, hostApp_, desc_.name);
            view->release();
        }
        return editor_->open();
    }

    void closeEditor() {
        if (editor_) editor_->close();
    }

    bool isEditorOpen() const {
        return editor_ && editor_->isOpen();
    }

    std::vector<std::pair<std::string, float>> editorIdle() {
        if (!editor_) return {};
        auto edits = editor_->idle();
        std::vector<std::pair<std::string, float>> out;
        for (const auto& [id, norm] : edits) {
            std::string sid = std::to_string(id);
            const auto* p = info_.param(sid);
            float val = p ? p->fromNormalized(static_cast<float>(norm)) : static_cast<float>(norm);
            out.emplace_back(sid, val);
        }
        return out;
    }

private:
    HMODULE module_;
    IComponent* comp_;
    IAudioProcessor* proc_;
    IEditController* ctrl_;
    Vst3Descriptor desc_;
    std::vector<Dsp::ParamInfo> paramInfos_;
    Dsp::PluginInfo info_;
    HostApplication* hostApp_;
    ParamChanges* inParams_;
    ParamChanges* outParams_;
    EventList* inEvents_;
    EventList* outEvents_;
    Vst3Editor* editor_;
    float sampleRate_;
    size_t maxBlock_;
    size_t channels_;
    size_t latency_;
    size_t tail_;
    bool offline_;
};

// ─── Module Loader & Registry ────────────────────────────────────────────────
class Vst3Registry {
public:
    static Vst3Registry& instance() {
        static Vst3Registry reg;
        return reg;
    }

    void addExtraPath(const fs::path& p) {
        std::lock_guard<std::mutex> lk(mtx_);
        extraDirs_.push_back(p);
        scanned_ = std::nullopt;
    }

    std::vector<fs::path> extraPaths() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return extraDirs_;
    }

    std::vector<Vst3Descriptor> scan() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!scanned_.has_value()) {
            scanned_ = doScan();
        }
        return *scanned_;
    }

    std::vector<Vst3Descriptor> rescan() {
        std::lock_guard<std::mutex> lk(mtx_);
        scanned_ = doScan();
        return *scanned_;
    }

    std::optional<std::vector<Vst3Descriptor>> cached() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return scanned_;
    }

    const Vst3Descriptor* findDescriptor(const std::string& id) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!scanned_.has_value()) scanned_ = doScan();
        for (const auto& d : *scanned_) {
            if (d.id == id) return &d;
        }
        return nullptr;
    }

    std::unique_ptr<Dsp::Plugin> create(const std::string& id) {
        auto parsedCid = parseId(id);
        if (!parsedCid.has_value()) return nullptr;

        const Vst3Descriptor* desc = findDescriptor(id);
        if (!desc) return nullptr;

        auto binaryPath = bundleBinary(desc->path);
        if (!binaryPath.has_value()) return nullptr;

#ifdef _WIN32
        HMODULE mod = LoadLibraryW(binaryPath->c_str());
        if (!mod) return nullptr;

        using InitDllFn = bool(PLUGIN_API*)();
        auto initDll = reinterpret_cast<InitDllFn>(GetProcAddress(mod, "InitDll"));
        if (initDll) initDll();

        using GetFactoryFn = IPluginFactory*(PLUGIN_API*)();
        auto getFactory = reinterpret_cast<GetFactoryFn>(GetProcAddress(mod, "GetPluginFactory"));
        if (!getFactory) return nullptr;

        IPluginFactory* factory = getFactory();
        if (!factory) return nullptr;

        IComponent* comp = nullptr;
        if (factory->createInstance(reinterpret_cast<FIDString>(parsedCid->data()), IComponent::iid, reinterpret_cast<void**>(&comp)) != kResultOk || !comp) {
            return nullptr;
        }

        auto* hostApp = new HostApplication();
        comp->initialize(static_cast<IHostApplication*>(hostApp));

        TUID ctrlCid{};
        IEditController* ctrl = nullptr;
        if (comp->getControllerClassId(ctrlCid) == kResultOk) {
            factory->createInstance(ctrlCid, IEditController::iid, reinterpret_cast<void**>(&ctrl));
        }
        if (!ctrl) {
            comp->queryInterface(IEditController::iid, reinterpret_cast<void**>(&ctrl));
        }

        if (ctrl) {
            ctrl->initialize(static_cast<IHostApplication*>(hostApp));

            IConnectionPoint* compCp = nullptr;
            IConnectionPoint* ctrlCp = nullptr;
            comp->queryInterface(IConnectionPoint::iid, reinterpret_cast<void**>(&compCp));
            ctrl->queryInterface(IConnectionPoint::iid, reinterpret_cast<void**>(&ctrlCp));
            if (compCp && ctrlCp) {
                compCp->connect(ctrlCp);
                ctrlCp->connect(compCp);
            }
            if (compCp) compCp->release();
            if (ctrlCp) ctrlCp->release();
        }

        IAudioProcessor* proc = nullptr;
        comp->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&proc));

        std::vector<Dsp::ParamInfo> pInfos;
        if (ctrl) {
            const int32 numParams = ctrl->getParameterCount();
            for (int32 i = 0; i < numParams && i < 4096; ++i) {
                ParameterInfo info{};
                if (ctrl->getParameterInfo(i, info) == kResultOk) {
                    if (info.flags & ParameterInfo::kIsHidden) continue;
                    Dsp::ParamInfo p{};
                    std::string idStr = std::to_string(info.id);
                    static std::unordered_map<std::string, std::string> sIdStrings;
                    sIdStrings[idStr] = idStr;
                    p.id = sIdStrings[idStr].c_str();

                    std::string titleStr = readC16(info.title);
                    static std::unordered_map<std::string, std::string> sTitleStrings;
                    sTitleStrings[titleStr] = titleStr;
                    p.name = sTitleStrings[titleStr].c_str();

                    p.defaultVal = static_cast<float>(info.defaultNormalizedValue);
                    p.minVal = 0.0f;
                    p.maxVal = (info.stepCount > 0) ? static_cast<float>(info.stepCount) : 1.0f;
                    p.unit = (info.stepCount == 1) ? Dsp::Unit::Toggle : (info.stepCount > 1) ? Dsp::Unit::Choice : Dsp::Unit::None;
                    pInfos.push_back(p);
                }
            }
        }

        hostApp->release();
        return std::make_unique<Vst3Plugin>(mod, comp, proc, ctrl, *desc, std::move(pInfos));
#else
        return nullptr;
#endif
    }

private:
    Vst3Registry() = default;

    std::vector<Vst3Descriptor> doScan() {
        std::vector<fs::path> dirs = defaultSearchPaths();
        dirs.insert(dirs.end(), extraDirs_.begin(), extraDirs_.end());

        std::vector<Vst3Descriptor> out;
        for (const auto& bundlePath : findBundles(dirs)) {
            auto binary = bundleBinary(bundlePath);
            if (!binary.has_value()) continue;
#ifdef _WIN32
            HMODULE mod = LoadLibraryW(binary->c_str());
            if (!mod) continue;

            using GetFactoryFn = IPluginFactory*(PLUGIN_API*)();
            auto getFactory = reinterpret_cast<GetFactoryFn>(GetProcAddress(mod, "GetPluginFactory"));
            if (!getFactory) {
                FreeLibrary(mod);
                continue;
            }

            IPluginFactory* factory = getFactory();
            if (!factory) {
                FreeLibrary(mod);
                continue;
            }

            IPluginFactory2* factory2 = nullptr;
            factory->queryInterface(IPluginFactory2::iid, reinterpret_cast<void**>(&factory2));

            IPluginFactory3* factory3 = nullptr;
            factory->queryInterface(IPluginFactory3::iid, reinterpret_cast<void**>(&factory3));

            const int32 numClasses = factory->countClasses();
            for (int32 i = 0; i < numClasses && i < 1024; ++i) {
                PClassInfo info{};
                if (factory->getClassInfo(i, &info) != kResultOk) continue;
                if (std::strcmp(info.category, kAudioModuleClass) != 0 &&
                    std::strcmp(info.category, kVstAudioEffectClass) != 0) {
                    continue;
                }

                PClassInfo2 info2{};
                bool hasInfo2 = factory2 && factory2->getClassInfo2(i, &info2) == kResultOk;

                std::string classIdHex = toHex(reinterpret_cast<const uint8_t*>(info.cid));
                std::string pluginId = std::string(kIdPrefix) + classIdHex;

                std::string subCatStr = hasInfo2 ? readC8(info2.subCategories) : "";
                auto subs = splitSubCategories(subCatStr);

                Vst3Descriptor desc{};
                desc.id = pluginId;
                desc.classId = classIdHex;
                desc.name = info.name[0] ? readC8(info.name) : "VST3 Plugin";
                desc.vendor = hasInfo2 && info2.vendor[0] ? readC8(info2.vendor) : std::string();
                desc.version = hasInfo2 && info2.version[0] ? readC8(info2.version) : std::string();
                desc.sdkVersion = hasInfo2 && info2.sdkVersion[0] ? readC8(info2.sdkVersion) : "";
                desc.subCategories = subs;
                desc.category = categoryFromSubCategories(subs);
                desc.isInstrument = isInstrument(subs);
                desc.path = bundlePath;

                if (std::find_if(out.begin(), out.end(), [&](const Vst3Descriptor& d) { return d.id == desc.id; }) == out.end()) {
                    out.push_back(desc);
                }
            }

            if (factory2) factory2->release();
            if (factory3) factory3->release();
            FreeLibrary(mod);
#endif
        }
        std::sort(out.begin(), out.end(), [](const Vst3Descriptor& a, const Vst3Descriptor& b) {
            std::string al = a.name, bl = b.name;
            for (char& c : al) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : bl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return al < bl;
        });
        return out;
    }

    mutable std::mutex mtx_;
    std::vector<fs::path> extraDirs_;
    std::optional<std::vector<Vst3Descriptor>> scanned_;
};

std::vector<Vst3Descriptor> scan() { return Vst3Registry::instance().scan(); }
std::vector<Vst3Descriptor> rescan() { return Vst3Registry::instance().rescan(); }
std::optional<std::vector<Vst3Descriptor>> cached() { return Vst3Registry::instance().cached(); }
void addExtraPath(const fs::path& p) { Vst3Registry::instance().addExtraPath(p); }
std::vector<fs::path> extraPaths() { return Vst3Registry::instance().extraPaths(); }
const Vst3Descriptor* findDescriptor(const std::string& id) { return Vst3Registry::instance().findDescriptor(id); }
std::unique_ptr<Dsp::Plugin> createInstance(const std::string& id) { return Vst3Registry::instance().create(id); }

void registerWithDsp() {
    // Scan standard paths once
    auto list = scan();
    (void)list;
}

void closeAllPluginWindows() {
    // Closes editor windows if active
}

} // namespace Vst3Host
