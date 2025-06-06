#include "dummy_encoder.h"
#include <esp_log.h>
#include <cstring>  // for std::memcpy

#define TAG "DummyEncoderWrapper"

DummyEncoderWrapper::DummyEncoderWrapper(int sample_rate, int channels, int duration_ms)
    : sample_rate_(sample_rate), duration_ms_(duration_ms) {
    frame_size_ = sample_rate / 1000 * channels * duration_ms;
    ESP_LOGI(TAG, "Created dummy encoder with frame size: %d", frame_size_);
}

void DummyEncoderWrapper::Encode(std::vector<int16_t>&& pcm, std::function<void(std::vector<uint8_t>&& data)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (in_buffer_.empty()) {
        in_buffer_ = std::move(pcm);
    } else {
        in_buffer_.reserve(in_buffer_.size() + pcm.size());
        in_buffer_.insert(in_buffer_.end(), pcm.begin(), pcm.end());
    }

    while (in_buffer_.size() >= frame_size_) {
        // Convert frame_size_ worth of int16_t data to uint8_t
        std::vector<uint8_t> raw_data(frame_size_ * sizeof(int16_t));
        std::memcpy(raw_data.data(), in_buffer_.data(), raw_data.size());

        if (handler) {
            handler(std::move(raw_data));
        }

        in_buffer_.erase(in_buffer_.begin(), in_buffer_.begin() + frame_size_);
    }
}

void DummyEncoderWrapper::ResetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    in_buffer_.clear();
    ESP_LOGI(TAG, "Reset dummy encoder state");
}