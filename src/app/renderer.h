#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace ciniva {

using Microsoft::WRL::ComPtr;

class Renderer {
public:
    bool init(HWND hwnd);
    void resize();
    void set_dpi(UINT dpi);

    ID2D1DeviceContext* begin();
    bool end();

    ID2D1DeviceContext* context() const { return context_.Get(); }
    IDWriteFactory* dwrite() const { return dwrite_.Get(); }
    ID2D1Factory1* factory() const { return factory_.Get(); }
    float scale() const { return static_cast<float>(dpi_) / 96.0f; }
    D2D1_SIZE_F size_dip() const;
    UINT generation() const { return generation_; }

private:
    bool create_device();
    void create_target();
    void release_device();

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    UINT generation_ = 0;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<ID2D1Device> device_;
    ComPtr<ID2D1DeviceContext> context_;
    ComPtr<ID2D1Bitmap1> target_;
};

}
