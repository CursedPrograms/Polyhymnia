// Polyhymnia - miniaudio playback device: enumerate outputs and keep a stream open (silence until the mix runs).
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include "miniaudio.h"

#include "audio.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace Audio {

static ma_context ctx;
static bool ctxOk = false;
static ma_device dev;
static bool devOpen = false;
static std::vector<DeviceInfo> devList;
static int activeIndex = -1;
static std::string activeName;
static unsigned bufFrames = 512;
static std::mutex devMtx;

static void callback(ma_device* device, void* output, const void* input, ma_uint32 frames) {
    (void)input;
    const ma_uint32 bytes = frames * ma_get_bytes_per_frame(device->playback.format, device->playback.channels);
    std::memset(output, 0, bytes);
}

static void refreshList() {
    devList.clear();
    ma_device_info* playback = nullptr;
    ma_uint32 n = 0;
    if (ma_context_get_devices(&ctx, &playback, &n, nullptr, nullptr) != MA_SUCCESS) return;
    for (ma_uint32 i = 0; i < n; ++i) {
        DeviceInfo d;
        d.index = (int)i;
        d.name = playback[i].name;
        devList.push_back(d);
    }
}

const std::vector<DeviceInfo>& playbackDevices() {
    if (!ctxOk) {
        if (ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS) {
            ctxOk = true;
            refreshList();
        }
    }
    return devList;
}

bool open(int deviceIndex, unsigned bufferFrames) {
    std::lock_guard<std::mutex> g(devMtx);
    if (devOpen) {
        ma_device_uninit(&dev);
        devOpen = false;
    }
    if (!ctxOk && ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return false;
    ctxOk = true;
    refreshList();
    ma_device_info* playback = nullptr;
    ma_uint32 nPlay = 0;
    ma_context_get_devices(&ctx, &playback, &nPlay, nullptr, nullptr);
    ma_device_id* picked = nullptr;
    activeIndex = deviceIndex;
    activeName = "Default";
    if (deviceIndex >= 0 && (ma_uint32)deviceIndex < nPlay) {
        picked = &playback[deviceIndex].id;
        activeName = playback[deviceIndex].name;
    }
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = callback;
    bufFrames = bufferFrames > 0 ? bufferFrames : 512;
    cfg.periodSizeInFrames = bufFrames;
    cfg.playback.pDeviceID = picked;
    if (ma_device_init(&ctx, &cfg, &dev) != MA_SUCCESS) return false;
    if (ma_device_start(&dev) != MA_SUCCESS) {
        ma_device_uninit(&dev);
        return false;
    }
    devOpen = true;
    return true;
}

void close() {
    std::lock_guard<std::mutex> g(devMtx);
    if (devOpen) {
        ma_device_uninit(&dev);
        devOpen = false;
    }
}

bool isOpen() { return devOpen; }
std::string activeDeviceName() { return activeName; }
unsigned bufferSize() { return bufFrames; }
int activeDeviceIndex() { return activeIndex; }

}