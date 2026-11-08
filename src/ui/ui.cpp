#include "ui/ui.h"

#include "core/text.h"
#include "ui/images.h"

#include <algorithm>
#include <cmath>

namespace ciniva::ui {

Id make_id(std::string_view tag, long long n) {
    uint64_t h = 1469598103934665603ull;
    for (char c : tag) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    h = (h ^ static_cast<uint64_t>(n)) * 1099511628211ull;
    return h ? h : 1;
}

Rect intersect(const Rect& a, const Rect& b) {
    Rect r = {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

Color with_alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

void Scroll::scroll_by(float delta, bool animate) { scroll_to(target + delta, animate); }

void Scroll::scroll_to(float value, bool animate) {
    target = std::clamp(value, 0.0f, max());
    if (!animate) offset = target;
}

bool Scroll::step(float dt) {
    target = std::clamp(target, 0.0f, max());
    if (std::fabs(target - offset) < 0.5f) {
        offset = target;
        return false;
    }
    offset += (target - offset) * (1.0f - std::exp(-dt * 16.0f));
    return true;
}

void Ui::begin_frame(ID2D1DeviceContext* ctx, double now) {
    ctx_ = ctx;
    dt_ = now_ > 0 ? static_cast<float>(std::min(now - now_, 0.1)) : 0.016f;
    now_ = now;
    wants_frame_ = false;
    regions_.clear();
    areas_.clear();
    area_stack_.clear();
    clips_.clear();
    if (generation_ != renderer_.generation() || !brush_) {
        generation_ = renderer_.generation();
        brush_.Reset();
        ctx_->CreateSolidColorBrush(theme::text, &brush_);
    }
}

void Ui::end_frame() {
    while (!clips_.empty()) pop_clip();
    prev_regions_ = std::move(regions_);
    prev_areas_ = std::move(areas_);
    regions_.clear();
    areas_.clear();
    if (pending_scroll_into_view_) {
        pending_scroll_into_view_ = false;
        for (auto& r : prev_regions_)
            if (r.id == focus_) {
                scroll_into_view(r);
                request_frame();
            }
    }
    auto* h = hit(mouse_);
    hover_ = mouse_inside_ && h ? h->id : 0;
}

void Ui::fill(const Rect& r, Color c) {
    brush_->SetColor(c);
    ctx_->FillRectangle(r, brush_.Get());
}

void Ui::fill_round(const Rect& r, float radius, Color c) {
    brush_->SetColor(c);
    ctx_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
}

void Ui::stroke_round(const Rect& r, float radius, Color c, float w) {
    brush_->SetColor(c);
    ctx_->DrawRoundedRectangle(D2D1::RoundedRect(inset(r, w / 2, w / 2), radius, radius), brush_.Get(), w);
}

void Ui::fill_gradient(const Rect& r, Color top, Color bottom) {
    D2D1_GRADIENT_STOP stops[] = {{0, top}, {1, bottom}};
    ComPtr<ID2D1GradientStopCollection> coll;
    ctx_->CreateGradientStopCollection(stops, 2, &coll);
    ComPtr<ID2D1LinearGradientBrush> b;
    ctx_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({r.left, r.top}, {r.left, r.bottom}), coll.Get(), &b);
    ctx_->FillRectangle(r, b.Get());
}

void Ui::fill_gradient_h(const Rect& r, Color left, Color right) {
    D2D1_GRADIENT_STOP stops[] = {{0, left}, {1, right}};
    ComPtr<ID2D1GradientStopCollection> coll;
    ctx_->CreateGradientStopCollection(stops, 2, &coll);
    ComPtr<ID2D1LinearGradientBrush> b;
    ctx_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({r.left, r.top}, {r.right, r.top}), coll.Get(), &b);
    ctx_->FillRectangle(r, b.Get());
}

void Ui::line(D2D1_POINT_2F a, D2D1_POINT_2F b, Color c, float w) {
    brush_->SetColor(c);
    ctx_->DrawLine(a, b, brush_.Get(), w);
}

void Ui::circle(D2D1_POINT_2F center, float radius, Color c) {
    brush_->SetColor(c);
    ctx_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get());
}

IDWriteTextFormat* Ui::format(Font f) {
    auto& slot = formats_[static_cast<int>(f)];
    if (slot) return slot.Get();
    static const wchar_t* icon_family = [this] {
        ComPtr<IDWriteFontCollection> fonts;
        renderer_.dwrite()->GetSystemFontCollection(&fonts);
        UINT32 index;
        BOOL exists = FALSE;
        fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists);
        return exists ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }();
    struct Spec {
        const wchar_t* family;
        float size;
        DWRITE_FONT_WEIGHT weight;
    };
    static const Spec specs[] = {
        {L"Segoe UI Variable Display", 34, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Display", 26, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Display", 20, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Text", 15.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Text", 14, DWRITE_FONT_WEIGHT_NORMAL},
        {L"Segoe UI Variable Text", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Text", 12.5f, DWRITE_FONT_WEIGHT_NORMAL},
        {L"Segoe UI Variable Text", 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {L"Segoe UI Variable Text", 11, DWRITE_FONT_WEIGHT_NORMAL},
        {icon_family, 16, DWRITE_FONT_WEIGHT_NORMAL},
        {icon_family, 12, DWRITE_FONT_WEIGHT_NORMAL},
    };
    const Spec& s = specs[static_cast<int>(f)];
    renderer_.dwrite()->CreateTextFormat(s.family, nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, s.size, L"en-us", &slot);
    return slot.Get();
}

static ComPtr<IDWriteTextLayout> make_layout(IDWriteFactory* dw, IDWriteTextFormat* fmt, std::wstring_view s, float w,
                                             float h, bool wrap) {
    ComPtr<IDWriteTextLayout> layout;
    dw->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), fmt, std::max(w, 0.0f), std::max(h, 0.0f), &layout);
    if (!layout) return layout;
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    dw->CreateEllipsisTrimmingSign(fmt, &ellipsis);
    layout->SetTrimming(&trim, ellipsis.Get());
    return layout;
}

void Ui::text(std::string_view s, const Rect& r, const TextStyle& style) { text(widen(s), r, style); }

void Ui::text(std::wstring_view s, const Rect& r, const TextStyle& style) {
    if (s.empty() || width(r) <= 0) return;
    IDWriteTextFormat* fmt = format(style.font);
    float h = height(r);
    if (style.max_lines > 0) {
        DWRITE_LINE_SPACING_METHOD method;
        FLOAT spacing = 0, baseline = 0;
        fmt->GetLineSpacing(&method, &spacing, &baseline);
        float line_h = spacing > 0 ? spacing : fmt->GetFontSize() * 1.33f;
        h = std::min(h, line_h * style.max_lines + 1);
    }
    auto layout = make_layout(renderer_.dwrite(), fmt, s, width(r), h, style.wrap);
    if (!layout) return;
    layout->SetTextAlignment(style.align == Align::Center     ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : style.align == Align::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                              : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(style.center_vertically && !style.wrap ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                                         : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    brush_->SetColor(style.color);
    ctx_->DrawTextLayout({r.left, r.top}, layout.Get(), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_SIZE_F Ui::measure(std::string_view s, Font font, float max_width, bool wrap, int max_lines) {
    std::wstring w = widen(s);
    auto layout = make_layout(renderer_.dwrite(), format(font), w, max_width, 100000, wrap);
    if (!layout) return {0, 0};
    DWRITE_TEXT_METRICS m = {};
    layout->GetMetrics(&m);
    float h = m.height;
    if (max_lines > 0 && m.lineCount > static_cast<UINT32>(max_lines)) h = h / m.lineCount * max_lines;
    return {m.widthIncludingTrailingWhitespace, h};
}

void Ui::image(const std::optional<std::string>& url, const Rect& r, float radius, int decode_width, Color placeholder,
               float opacity) {
    Rect visible = intersect(r, clip());
    if (width(visible) <= 0 || height(visible) <= 0) return;
    float alpha = 0;
    ID2D1Bitmap* bmp = nullptr;
    if (url) bmp = images_.get(ctx_, renderer_.generation(), *url, decode_width, now_, &alpha);
    if (alpha < 1) {
        if (alpha > 0) request_frame();
        fill_round(r, radius, with_alpha(placeholder, opacity));
    }
    if (!bmp) return;
    auto size = bmp->GetSize();
    float sx = width(r) / size.width, sy = height(r) / size.height;
    float scale = std::max(sx, sy);
    float dx = r.left + (width(r) - size.width * scale) / 2;
    float dy = r.top + (height(r) - size.height * scale) / 2;
    ComPtr<ID2D1BitmapBrush> brush;
    D2D1_BITMAP_BRUSH_PROPERTIES props = {D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR};
    ctx_->CreateBitmapBrush(bmp, props, D2D1::BrushProperties(alpha * opacity, D2D1::Matrix3x2F::Scale(scale, scale) *
                                                                                    D2D1::Matrix3x2F::Translation(dx, dy)),
                            &brush);
    if (!brush) return;
    if (radius > 0) ctx_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get());
    else ctx_->FillRectangle(r, brush.Get());
}

void Ui::push_clip(const Rect& r) {
    Rect c = intersect(r, clip());
    clips_.push_back(c);
    ctx_->PushAxisAlignedClip(c, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Ui::pop_clip() {
    if (clips_.empty()) return;
    clips_.pop_back();
    ctx_->PopAxisAlignedClip();
}

ButtonState Ui::region(Id id, const Rect& r, std::function<void()> on_click, bool focusable) {
    Rect visible = intersect(r, clip());
    ScrollArea* area = area_stack_.empty() ? nullptr : area_stack_.back();
    if (width(visible) > 0 && height(visible) > 0)
        regions_.push_back({id, visible, r, std::move(on_click), focusable, area, nullptr, nullptr});
    else if (focus_ == id)
        regions_.push_back({id, visible, r, std::move(on_click), focusable, area, nullptr, nullptr});
    ButtonState st;
    st.hover = hover_ == id && mouse_inside_ && contains(visible, mouse_);
    st.pressed = pressed_ == id && st.hover;
    st.focused = keyboard_focus_ && focus_ == id;
    return st;
}

ScrollArea* Ui::scroll_area(const Rect& viewport, Scroll& scroll, bool horizontal) {
    ScrollArea* parent = area_stack_.empty() ? nullptr : area_stack_.back();
    areas_.push_back(std::make_unique<ScrollArea>(ScrollArea{intersect(viewport, clip()), &scroll, horizontal, parent}));
    ScrollArea* a = areas_.back().get();
    scroll.viewport = horizontal ? width(viewport) : height(viewport);
    if (scroll.step(dt_)) request_frame();
    area_stack_.push_back(a);
    push_clip(viewport);
    return a;
}

void Ui::end_scroll_area() {
    pop_clip();
    if (!area_stack_.empty()) area_stack_.pop_back();
}

void Ui::focus_ring(const Rect& r, float radius) { stroke_round(inset(r, -3, -3), radius + 3, theme::accent, 2); }

const Region* Ui::hit(D2D1_POINT_2F p) const {
    for (auto it = prev_regions_.rbegin(); it != prev_regions_.rend(); ++it)
        if (contains(it->rect, p)) return &*it;
    return nullptr;
}

bool Ui::on_mouse_move(D2D1_POINT_2F p) {
    mouse_ = p;
    mouse_inside_ = true;
    auto* h = hit(p);
    Id id = h ? h->id : 0;
    if (id == hover_) return false;
    hover_ = id;
    return true;
}

void Ui::on_mouse_leave() {
    mouse_inside_ = false;
    hover_ = 0;
}

bool Ui::on_mouse_down(D2D1_POINT_2F p) {
    keyboard_focus_ = false;
    auto* h = hit(p);
    pressed_ = h ? h->id : 0;
    if (h && h->on_press) h->on_press(p);
    return h != nullptr;
}

bool Ui::on_mouse_up(D2D1_POINT_2F p) {
    auto* h = hit(p);
    Id was = pressed_;
    pressed_ = 0;
    if (h && h->id == was && h->on_click) {
        auto fn = h->on_click;
        fn();
        return true;
    }
    return was != 0;
}

bool Ui::on_double_click(D2D1_POINT_2F p) {
    auto* h = hit(p);
    if (h && h->on_double_click) {
        auto fn = h->on_double_click;
        fn();
        return true;
    }
    return false;
}

bool Ui::on_wheel(D2D1_POINT_2F p, float delta, bool horizontal, bool precise) {
    ScrollArea* best = nullptr;
    for (auto it = prev_areas_.rbegin(); it != prev_areas_.rend(); ++it) {
        auto* a = it->get();
        if (contains(a->viewport, p) && a->horizontal == horizontal && a->scroll->max() > 0) {
            best = a;
            break;
        }
    }
    if (!best) return false;
    best->scroll->scroll_by(-delta, !precise);
    return true;
}

bool Ui::hovering_clickable() const {
    auto* h = hit(mouse_);
    return mouse_inside_ && h && h->on_click;
}

void Ui::reset_interaction() {
    pressed_ = 0;
    hover_ = 0;
    focus_ = 0;
    prev_regions_.clear();
    prev_areas_.clear();
}

void Ui::scroll_into_view(const Region& r) {
    const float margin = 24;
    for (ScrollArea* a = r.area; a; a = a->parent) {
        const Rect& v = a->viewport;
        if (a->horizontal) {
            if (r.full.left < v.left) a->scroll->scroll_by(r.full.left - v.left - margin, true);
            else if (r.full.right > v.right) a->scroll->scroll_by(r.full.right - v.right + margin, true);
        } else {
            if (r.full.top < v.top) a->scroll->scroll_by(r.full.top - v.top - margin, true);
            else if (r.full.bottom > v.bottom) a->scroll->scroll_by(r.full.bottom - v.bottom + margin, true);
        }
    }
}

void Ui::move_focus(int dx, int dy) {
    std::vector<const Region*> candidates;
    for (auto& r : prev_regions_)
        if (r.focusable && r.on_click) candidates.push_back(&r);
    if (candidates.empty()) return;
    const Region* current = nullptr;
    for (auto* r : candidates)
        if (r->id == focus_) current = r;
    if (!current) {
        focus_ = candidates.front()->id;
        pending_scroll_into_view_ = true;
        return;
    }
    float cx = (current->full.left + current->full.right) / 2, cy = (current->full.top + current->full.bottom) / 2;
    const Region* best = nullptr;
    float best_score = 1e30f;
    for (auto* r : candidates) {
        if (r == current) continue;
        float x = (r->full.left + r->full.right) / 2, y = (r->full.top + r->full.bottom) / 2;
        float along = dx ? (x - cx) * dx : (y - cy) * dy;
        float across = dx ? std::fabs(y - cy) : std::fabs(x - cx);
        if (along <= 1) continue;
        float score = along + across * 2.5f;
        if (score < best_score) {
            best_score = score;
            best = r;
        }
    }
    if (best) {
        focus_ = best->id;
        pending_scroll_into_view_ = true;
    }
}

bool Ui::on_key(WPARAM key) {
    switch (key) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
        keyboard_focus_ = true;
        move_focus(key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : 0, key == VK_UP ? -1 : key == VK_DOWN ? 1 : 0);
        return true;
    case VK_TAB: {
        keyboard_focus_ = true;
        std::vector<const Region*> list;
        for (auto& r : prev_regions_)
            if (r.focusable && r.on_click) list.push_back(&r);
        if (list.empty()) return true;
        bool back = GetKeyState(VK_SHIFT) < 0;
        auto it = std::find_if(list.begin(), list.end(), [&](const Region* r) { return r->id == focus_; });
        size_t i = it == list.end() ? 0 : (it - list.begin() + (back ? list.size() - 1 : 1)) % list.size();
        focus_ = list[i]->id;
        pending_scroll_into_view_ = true;
        return true;
    }
    case VK_RETURN:
    case VK_SPACE:
        for (auto& r : prev_regions_)
            if (r.id == focus_ && r.on_click && keyboard_focus_) {
                auto fn = r.on_click;
                fn();
                return true;
            }
        return false;
    }
    return false;
}

}
