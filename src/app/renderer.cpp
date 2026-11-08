#include "app/renderer.h"

#include <algorithm>
#include <atomic>

namespace ciniva {

static std::atomic<UINT> g_generation{0};

bool Renderer::init(HWND hwnd, bool composition) {
    hwnd_ = hwnd;
    composition_ = composition;
    dpi_ = GetDpiForWindow(hwnd);
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(factory_.GetAddressOf()))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    return create_device();
}

static SIZE client_size(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    return {std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top)};
}

bool Renderer::create_device() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgi_device;
    d3d_.As(&dxgi_device);
    if (FAILED(factory_->CreateDevice(dxgi_device.Get(), &device_))) return false;
    if (FAILED(device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return false;
    generation_ = ++g_generation;

    ComPtr<IDXGIAdapter> adapter;
    dxgi_device->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> dxgi_factory;
    adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (composition_) {
        SIZE sz = client_size(hwnd_);
        desc.Width = sz.cx;
        desc.Height = sz.cy;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(dxgi_factory->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr, &swapchain_))) return false;
        if (FAILED(DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
        dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcomp_target_);
        dcomp_->CreateVisual(&dcomp_visual_);
        dcomp_visual_->SetContent(swapchain_.Get());
        dcomp_target_->SetRoot(dcomp_visual_.Get());
        dcomp_->Commit();
    } else {
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (FAILED(dxgi_factory->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &desc, nullptr, nullptr, &swapchain_)))
            return false;
        dxgi_factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    }
    create_target();
    return true;
}

void Renderer::create_target() {
    ComPtr<IDXGISurface> surface;
    swapchain_->GetBuffer(0, IID_PPV_ARGS(&surface));
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, composition_ ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    context_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_);
    context_->SetTarget(target_.Get());
    context_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void Renderer::release_device() {
    if (context_) context_->SetTarget(nullptr);
    target_.Reset();
    context_.Reset();
    device_.Reset();
    dcomp_visual_.Reset();
    dcomp_target_.Reset();
    dcomp_.Reset();
    swapchain_.Reset();
    d3d_.Reset();
}

void Renderer::resize() {
    if (!swapchain_) return;
    context_->SetTarget(nullptr);
    target_.Reset();
    SIZE sz = composition_ ? client_size(hwnd_) : SIZE{0, 0};
    if (FAILED(swapchain_->ResizeBuffers(0, sz.cx, sz.cy, DXGI_FORMAT_UNKNOWN, 0))) {
        release_device();
        create_device();
        return;
    }
    create_target();
}

void Renderer::set_dpi(UINT dpi) {
    dpi_ = dpi;
    if (context_) context_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
}

D2D1_SIZE_F Renderer::size_dip() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float s = scale();
    return D2D1::SizeF(static_cast<float>(rc.right - rc.left) / s, static_cast<float>(rc.bottom - rc.top) / s);
}

ID2D1DeviceContext* Renderer::begin() {
    if (!context_ && !create_device()) return nullptr;
    context_->BeginDraw();
    return context_.Get();
}

bool Renderer::end() {
    HRESULT hr = context_->EndDraw();
    if (SUCCEEDED(hr)) hr = swapchain_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        release_device();
        return false;
    }
    return SUCCEEDED(hr);
}

}
