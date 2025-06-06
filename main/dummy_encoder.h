#ifndef _DUMMY_ENCODER_WRAPPER_H_
#define _DUMMY_ENCODER_WRAPPER_H_

#include <functional>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>

#define MAX_DUMMY_PACKET_SIZE 1000

class DummyEncoderWrapper {
public:
    DummyEncoderWrapper(int sample_rate, int channels, int duration_ms = 60);
    ~DummyEncoderWrapper() = default;

    inline int sample_rate() const {
        return sample_rate_;
    }

    inline int duration_ms() const {
        return duration_ms_;
    }

    void SetDtx(bool enable) {} // No-op
    void SetComplexity(int complexity) {} // No-op
    void Encode(std::vector<int16_t>&& pcm, std::function<void(std::vector<uint8_t>&& data)> handler);
    bool IsBufferEmpty() const { return in_buffer_.empty(); }
    void ResetState();

private:
    std::mutex mutex_;
    const int sample_rate_;
    const int duration_ms_;
    int frame_size_;
    std::vector<int16_t> in_buffer_;
};

#endif // _DUMMY_ENCODER_WRAPPER_H_