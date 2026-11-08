#include "ui/images.h"

#include "app/dispatch.h"
#include "core/http.h"
#include "core/settings.h"
#include "core/text.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace ciniva::ui {

namespace {

constexpr int kMaxConcurrent = 6;
constexpr size_t kMemoryBudget = 384ull * 1024 * 1024;

std::string cache_key(const std::string& url, int decode_width) {
    std::string s = url;
    if (auto t = s.find("token="); t != std::string::npos) {
        auto end = s.find('&', t);
        s.erase(t, end == std::string::npos ? std::string::npos : end - t + 1);
    }
    uint64_t h = 1469598103934665603ull;
    for (char c : s) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx-%d", static_cast<unsigned long long>(h), decode_width);
    return buf;
}

std::wstring disk_dir() {
    static std::wstring dir = [] {
        std::wstring d = Settings::data_dir() + L"\\images";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

IWICImagingFactory* wic() {
    static ComPtr<IWICImagingFactory> factory = [] {
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return factory.Get();
}

ComPtr<IWICBitmap> decode(const std::string& bytes, int decode_width) {
    IWICImagingFactory* f = wic();
    if (!f || bytes.empty()) return nullptr;
    ComPtr<IWICStream> stream;
    f->CreateStream(&stream);
    if (!stream || FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),
                                                       static_cast<DWORD>(bytes.size()))))
        return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) return nullptr;
    ComPtr<IWICBitmapSource> source = frame;
    if (decode_width > 0 && w > static_cast<UINT>(decode_width)) {
        ComPtr<IWICBitmapScaler> scaler;
        f->CreateBitmapScaler(&scaler);
        UINT nh = std::max(1u, static_cast<UINT>(static_cast<double>(h) * decode_width / w));
        if (scaler && SUCCEEDED(scaler->Initialize(frame.Get(), decode_width, nh, WICBitmapInterpolationModeHighQualityCubic)))
            source = scaler;
    }
    ComPtr<IWICFormatConverter> conv;
    f->CreateFormatConverter(&conv);
    if (!conv || FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)))
        return nullptr;
    ComPtr<IWICBitmap> bitmap;
    f->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &bitmap);
    return bitmap;
}

std::string read_file(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}

ID2D1Bitmap* ImageCache::get(ID2D1DeviceContext* ctx, unsigned generation, const std::string& url, int decode_width,
                             double now, float* alpha) {
    *alpha = 0;
    std::string key = url + "#" + std::to_string(decode_width);
    auto [it, inserted] = entries_.try_emplace(key);
    Entry& e = it->second;
    e.last_used = now;
    if (inserted) {
        e.decode_width = decode_width;
        queue_.push_back(key);
        pump();
        return nullptr;
    }
    if (e.state == State::Queued) {
        // Most recently requested first, so on-screen images win over ones scrolled past.
        auto q = std::find(queue_.begin(), queue_.end(), key);
        if (q != queue_.end() && q != queue_.end() - 1) {
            queue_.erase(q);
            queue_.push_back(key);
        }
        return nullptr;
    }
    if (e.state != State::Ready) return nullptr;
    if (!e.gpu || e.generation != generation) {
        e.gpu.Reset();
        ctx->CreateBitmapFromWicBitmap(e.cpu.Get(), nullptr, &e.gpu);
        e.generation = generation;
        if (!e.gpu) return nullptr;
    }
    if (e.loaded_at < 0) e.loaded_at = now;
    *alpha = static_cast<float>(std::clamp((now - e.loaded_at) / 0.18, 0.0, 1.0));
    return e.gpu.Get();
}

void ImageCache::pump() {
    while (active_ < kMaxConcurrent && !queue_.empty()) {
        std::string key = queue_.back();
        queue_.pop_back();
        auto it = entries_.find(key);
        if (it == entries_.end()) continue;
        it->second.state = State::Loading;
        ++active_;
        std::string url = key.substr(0, key.rfind('#'));
        int width = it->second.decode_width;
        dispatch::async(
            [url, width]() -> ComPtr<IWICBitmap> {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                std::wstring path = disk_dir() + L"\\" + widen(cache_key(url, 0));
                std::string bytes = read_file(path);
                if (bytes.empty()) {
                    HttpRequest req;
                    req.url = url;
                    req.headers.push_back({"User-Agent", "Ciniva-Windows/1.0"});
                    auto r = http_send(req);
                    if (!r.ok()) return nullptr;
                    bytes = std::move(r.body);
                    std::wstring tmp = path + L".tmp";
                    {
                        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                    }
                    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
                }
                return decode(bytes, width);
            },
            [this, key](ComPtr<IWICBitmap> bitmap) {
                --active_;
                auto it = entries_.find(key);
                if (it != entries_.end()) {
                    Entry& e = it->second;
                    if (bitmap) {
                        e.cpu = bitmap;
                        e.state = State::Ready;
                        UINT w = 0, h = 0;
                        bitmap->GetSize(&w, &h);
                        e.bytes = static_cast<size_t>(w) * h * 4;
                        total_bytes_ += e.bytes;
                    } else {
                        e.state = State::Failed;
                    }
                }
                evict();
                pump();
                if (on_loaded_) on_loaded_();
            });
    }
}

void ImageCache::evict() {
    if (total_bytes_ <= kMemoryBudget) return;
    std::vector<std::pair<double, std::string>> ready;
    for (auto& [k, e] : entries_)
        if (e.state == State::Ready) ready.push_back({e.last_used, k});
    std::sort(ready.begin(), ready.end());
    for (auto& [t, k] : ready) {
        if (total_bytes_ <= kMemoryBudget * 3 / 4) break;
        total_bytes_ -= entries_[k].bytes;
        entries_.erase(k);
    }
}

void ImageCache::clear_memory() {
    for (auto& [k, e] : entries_) e.gpu.Reset();
}

}
