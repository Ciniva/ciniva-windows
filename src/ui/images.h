#pragma once

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

namespace ciniva::ui {

using Microsoft::WRL::ComPtr;

class ImageCache {
public:
    explicit ImageCache(std::function<void()> on_loaded) : on_loaded_(std::move(on_loaded)) {}

    // Returns the bitmap when decoded (alpha ramps 0 to 1 over the fade-in) or nullptr while it loads.
    ID2D1Bitmap* get(ID2D1DeviceContext* ctx, unsigned generation, const std::string& url, int decode_width, double now,
                     float* alpha);
    void clear_memory();

private:
    enum class State { Queued, Loading, Ready, Failed };
    struct Entry {
        State state = State::Queued;
        int decode_width = 0;
        ComPtr<IWICBitmap> cpu;
        ComPtr<ID2D1Bitmap> gpu;
        unsigned generation = 0;
        double loaded_at = -1;
        double last_used = 0;
        size_t bytes = 0;
    };

    void pump();
    void evict();

    std::function<void()> on_loaded_;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::string> queue_;
    int active_ = 0;
    size_t total_bytes_ = 0;
};

}
