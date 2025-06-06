#include "dummy_decoder.h"
#include <esp_log.h>
#include <cstring>  // for std::memcpy

#define TAG "DummyDecoderWrapper"

DummyDecoderWrapper::DummyDecoderWrapper(int sample_rate, int channels, int duration_ms)
    : sample_rate_(sample_rate), duration_ms_(duration_ms) {
    frame_size_ = sample_rate / 1000 * channels * duration_ms;
    ESP_LOGI(TAG, "Created dummy decoder with frame size: %d", frame_size_);
}

bool DummyDecoderWrapper::Decode(std::vector<uint8_t>&& input, std::vector<int16_t>& pcm) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Simply copy input data to output buffer
    pcm.resize(input.size() / sizeof(int16_t));
    std::memcpy(pcm.data(), input.data(), input.size());
    
    ESP_LOGV(TAG, "Decoded %zu bytes to %zu samples", input.size(), pcm.size());
    return true;
}

void DummyDecoderWrapper::ResetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    ESP_LOGI(TAG, "Reset dummy decoder state");
}