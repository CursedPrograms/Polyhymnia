// Polyhymnia - playback device through miniaudio (output opens for monitoring in later phases).
#pragma once
#include <string>
#include <vector>

namespace Audio {
struct DeviceInfo {
    int index = -1;
    std::string name;
};
const std::vector<DeviceInfo>& playbackDevices();
bool open(int deviceIndex, unsigned bufferFrames);
void close();
bool isOpen();
std::string activeDeviceName();
unsigned bufferSize();
int activeDeviceIndex();
}