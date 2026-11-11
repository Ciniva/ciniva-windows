#pragma once

#include "app/renderer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ciniva::ui {

using Rect = D2D1_RECT_F;
using Color = D2D1_COLOR_F;

namespace theme {
constexpr Color background = {0.067f, 0.067f, 0.075f, 1};
constexpr Color sidebar = {0.090f, 0.090f, 0.102f, 1};
constexpr Color surface = {0.122f, 0.122f, 0.137f, 1};
constexpr Color surface_hover = {0.165f, 0.165f, 0.184f, 1};
constexpr Color stroke = {1, 1, 1, 0.08f};
constexpr Color text = {0.95f, 0.95f, 0.96f, 1};
constexpr Color secondary = {0.62f, 0.62f, 0.66f, 1};
constexpr Color tertiary = {0.42f, 0.42f, 0.46f, 1};
constexpr Color accent = {0.961f, 0.722f, 0.290f, 1};
constexpr Color accent_text = {0.10f, 0.08f, 0.04f, 1};
constexpr Color error = {0.94f, 0.36f, 0.33f, 1};
constexpr Color success = {0.38f, 0.80f, 0.47f, 1};
constexpr Color scrim = {0, 0, 0, 0.55f};
}

enum class Font { Huge, Title, Heading, Subheading, Body, BodyBold, Small, SmallBold, Caption, Icon, IconSmall };

enum class Align { Leading, Center, Trailing };

struct TextStyle {
    Font font = Font::Body;
    Color color = theme::text;
    Align align = Align::Leading;
    bool wrap = false;
    bool center_vertically = true;
    int max_lines = 0;
};

using Id = uint64_t;
Id make_id(std::string_view tag, long long n = 0);

inline Rect rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
inline float width(const Rect& r) { return r.right - r.left; }
inline float height(const Rect& r) { return r.bottom - r.top; }
inline bool contains(const Rect& r, D2D1_POINT_2F p) { return r.right > r.left && r.bottom > r.top && p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
inline Rect inset(const Rect& r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }
Rect intersect(const Rect& a, const Rect& b);
Color with_alpha(Color c, float a);
Color mix(Color a, Color b, float t);

class ImageCache;

struct Scroll {
    float offset = 0;
    float target = 0;
    float content = 0;
    float viewport = 0;

    float max() const { return content > viewport ? content - viewport : 0; }
    void scroll_by(float delta, bool animate);
    void scroll_to(float value, bool animate);
    bool step(float dt);
};

struct ScrollArea {
    Rect viewport;
    Scroll* scroll;
    bool horizontal;
    ScrollArea* parent;
};

struct Region {
    Id id;
    Rect rect;
    Rect full;
    std::function<void()> on_click;
    bool focusable;
    ScrollArea* area;
    std::function<void()> on_double_click;
    std::function<void(D2D1_POINT_2F)> on_press;
};

struct ButtonState {
    bool hover = false;
    bool pressed = false;
    bool focused = false;
};

class Ui {
public:
    Ui(Renderer& renderer, ImageCache& images) : renderer_(renderer), images_(images) {}

    void begin_frame(ID2D1DeviceContext* ctx, double now);
    void end_frame();

    ID2D1DeviceContext* ctx() const { return ctx_; }
    Renderer& renderer() { return renderer_; }
    ImageCache& images() { return images_; }
    double now() const { return now_; }
    float dt() const { return dt_; }
    D2D1_POINT_2F mouse() const { return mouse_; }
    bool mouse_inside() const { return mouse_inside_; }

    void fill(const Rect& r, Color c);
    void fill_round(const Rect& r, float radius, Color c);
    void stroke_round(const Rect& r, float radius, Color c, float width = 1);
    void fill_gradient(const Rect& r, Color top, Color bottom);
    void fill_gradient_h(const Rect& r, Color left, Color right);
    void line(D2D1_POINT_2F a, D2D1_POINT_2F b, Color c, float width = 1);
    void circle(D2D1_POINT_2F center, float radius, Color c);

    void text(std::string_view s, const Rect& r, const TextStyle& style);
    void text(std::wstring_view s, const Rect& r, const TextStyle& style);
    D2D1_SIZE_F measure(std::string_view s, Font font, float max_width = 100000, bool wrap = false, int max_lines = 0);
    IDWriteTextFormat* format(Font f);

    void image(const std::optional<std::string>& url, const Rect& r, float radius, int decode_width,
               Color placeholder = theme::surface, float opacity = 1);

    bool image_tile(const std::string& url, const Rect& source_px, const Rect& dest);
    // Draws the image scaled to fit inside r; false while it loads or when it failed.
    bool logo(const std::optional<std::string>& url, const Rect& r, int decode_width);

    void push_clip(const Rect& r);
    void pop_clip();
    Rect clip() const { return clips_.empty() ? Rect{-1e6f, -1e6f, 1e6f, 1e6f} : clips_.back(); }

    ButtonState region(Id id, const Rect& r, std::function<void()> on_click, bool focusable = true);
    Region* last_region() { return regions_.empty() ? nullptr : &regions_.back(); }
    ScrollArea* scroll_area(const Rect& viewport, Scroll& scroll, bool horizontal);
    void end_scroll_area();

    void focus_ring(const Rect& r, float radius);
    void request_frame() { wants_frame_ = true; }
    bool wants_frame() const { return wants_frame_; }

    bool on_mouse_move(D2D1_POINT_2F p);
    void on_mouse_leave();
    bool on_mouse_down(D2D1_POINT_2F p);
    bool on_mouse_up(D2D1_POINT_2F p);
    bool on_double_click(D2D1_POINT_2F p);
    bool on_wheel(D2D1_POINT_2F p, float delta, bool horizontal, bool precise);
    bool on_key(WPARAM key);
    bool hovering_clickable() const;
    void clear_focus() { focus_ = 0; }
    void set_focus(Id id) { focus_ = id; pending_scroll_into_view_ = true; }
    Id focus() const { return focus_; }
    void reset_interaction();

private:
    const Region* hit(D2D1_POINT_2F p) const;
    void move_focus(int dx, int dy);
    void scroll_into_view(const Region& r);

    Renderer& renderer_;
    ImageCache& images_;
    ID2D1DeviceContext* ctx_ = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> formats_[11];
    UINT generation_ = 0;
    double now_ = 0;
    float dt_ = 0;
    D2D1_POINT_2F mouse_ = {-1, -1};
    bool mouse_inside_ = false;
    std::vector<Rect> clips_;
    std::vector<Region> regions_;
    std::vector<Region> prev_regions_;
    std::vector<std::unique_ptr<ScrollArea>> areas_;
    std::vector<std::unique_ptr<ScrollArea>> prev_areas_;
    std::vector<ScrollArea*> area_stack_;
    Id hover_ = 0;
    Id pressed_ = 0;
    Id focus_ = 0;
    bool keyboard_focus_ = false;
    bool pending_scroll_into_view_ = false;
    bool wants_frame_ = false;
};

}
