#ifndef _DUMMY_DECODER_WRAPPER_H_
#define _DUMMY_DECODER_WRAPPER_H_

#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>

class DummyDecoderWrapper {
public:
    DummyDecoderWrapper(int sample_rate, int channels, int duration_ms = 60);
    ~DummyDecoderWrapper() = default;

    bool Decode(std::vector<uint8_t>&& input, std::vector<int16_t>& pcm);
    void ResetState();

    inline int sample_rate() const {
        return sample_rate_;
    }

    inline int duration_ms() const {
        return duration_ms_;
    }

private:
    std::mutex mutex_;
    int frame_size_;
    int sample_rate_;
    int duration_ms_;
};

#endif // _DUMMY_DECODER_WRAPPER_H_