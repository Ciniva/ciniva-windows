#include "screens/shell_screen.h"

#include "app/dispatch.h"
#include "core/text.h"
#include "screens/pages.h"
#include "ui/widgets.h"

#include <chrono>
#include <thread>

namespace ciniva {

using namespace ui;

namespace {
constexpr float kSidebar = 228;
}

ShellScreen::ShellScreen(HWND hwnd, AppModel& model) : hwnd_(hwnd), model_(model) {
    TextField::Options o;
    o.placeholder = L"Search movies & shows";
    o.on_change = [this] { search_changed(); };
    o.on_escape = [this] {
        search_.set_text(L"");
        search_changed();
        SetFocus(hwnd_);
    };
    o.on_submit = [this] { SetFocus(hwnd_); };
    search_.create(hwnd, o);
}

Nav ShellScreen::nav() {
    return Nav{model_, [this](std::unique_ptr<Page> p) { push(std::move(p)); }, [this] { back(); }};
}

void ShellScreen::on_show() {
    if (stack_.empty()) select(Section::Home);
}

void ShellScreen::on_hide() { search_.hide(); }

void ShellScreen::select(Section s) {
    section_ = s;
    stack_.clear();
    switch (s) {
    case Section::Home: stack_.push_back(make_home_page(nav())); break;
    case Section::Movies: stack_.push_back(make_library_page(nav(), LibraryKind::Movies)); break;
    case Section::Shows: stack_.push_back(make_library_page(nav(), LibraryKind::Shows)); break;
    }
    if (!query_.empty()) {
        query_.clear();
        search_page_.reset();
        search_.set_text(L"");
    }
}

void ShellScreen::push(std::unique_ptr<Page> page) {
    if (search_page_) {
        stack_.push_back(std::move(search_page_));
        query_.clear();
        search_.set_text(L"");
    }
    stack_.push_back(std::move(page));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool ShellScreen::back() {
    if (search_page_) {
        search_page_.reset();
        query_.clear();
        search_.set_text(L"");
        return true;
    }
    if (stack_.size() <= 1) return false;
    stack_.pop_back();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void ShellScreen::search_changed() {
    std::string q = trim(search_.text_utf8());
    int gen = ++search_generation_;
    std::weak_ptr<bool> alive = alive_;
    dispatch::background([this, alive, gen, q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        dispatch::post([this, alive, gen, q] {
            if (alive.expired() || gen != search_generation_) return;
            query_ = q;
            search_page_ = q.empty() ? nullptr : make_search_page(nav(), q);
        });
    });
}

bool ShellScreen::on_key(WPARAM key) {
    if (key == VK_ESCAPE || key == VK_BACK || key == VK_BROWSER_BACK) return back();
    if (key == 'F' && GetKeyState(VK_CONTROL) < 0) {
        search_.focus();
        search_.select_all();
        return true;
    }
    if (key == VK_F5) {
        model_.library_changed();
        return true;
    }
    return false;
}

void ShellScreen::paint_sidebar(Ui& ui, const Rect& r) {
    ui.fill(r, theme::sidebar);
    ui.line({r.right - 0.5f, r.top}, {r.right - 0.5f, r.bottom}, theme::stroke);
    float x = r.left + 14, w = width(r) - 28;
    float y = r.top + 22;
    ui.text("Ciniva", rect(x + 6, y, w, 30), {Font::Heading, theme::accent});
    y += 46;
    search_.paint(ui, rect(x, y, w, 36));
    y += 52;

    auto item = [&](const char* label, const wchar_t* glyph, bool on, Id id, std::function<void()> fn) {
        Rect row = rect(x, y, w, 38);
        auto st = ui.region(id, row, std::move(fn));
        if (on || st.hover) ui.fill_round(row, 8, on ? Color{1, 1, 1, 0.10f} : Color{1, 1, 1, 0.05f});
        if (on) ui.fill_round(rect(row.left, row.top + 10, 3, 18), 1.5f, theme::accent);
        widgets::glyph(ui, glyph, rect(row.left + 10, row.top, 24, 38), on ? theme::accent : theme::secondary);
        ui.text(label, rect(row.left + 44, row.top, w - 50, 38), {on ? Font::BodyBold : Font::Body, on ? theme::text : Color{0.85f, 0.85f, 0.87f, 1}});
        if (st.focused) ui.focus_ring(row, 8);
        y += 42;
    };
    bool searching = search_page_ != nullptr;
    item("Home", icon::home, !searching && section_ == Section::Home, make_id("nav.home"), [this] { select(Section::Home); });
    y += 10;
    ui.text("LIBRARY", rect(x + 10, y, w, 18), {Font::Caption, theme::tertiary});
    y += 24;
    item("Movies", icon::movie, !searching && section_ == Section::Movies, make_id("nav.movies"), [this] { select(Section::Movies); });
    item("Shows", icon::tv, !searching && section_ == Section::Shows, make_id("nav.shows"), [this] { select(Section::Shows); });

    float by = r.bottom - 112;
    ui.line({r.left + 14, by}, {r.right - 14, by}, theme::stroke);
    by += 12;
    std::string host = model_.client ? model_.client->base_url() : "";
    if (auto p = host.find("://"); p != std::string::npos) host = host.substr(p + 3);
    ui.circle({x + 18, by + 20}, 16, theme::surface_hover);
    widgets::glyph(ui, icon::person, rect(x + 2, by + 4, 32, 32), theme::secondary, Font::IconSmall);
    ui.text(model_.user ? model_.user->username : "", rect(x + 44, by + 2, w - 44, 20), {Font::BodyBold});
    ui.text(host, rect(x + 44, by + 21, w - 44, 18), {Font::Caption, theme::secondary});
    by += 48;
    float bw = (w - 8) / 2;
    widgets::button(ui, make_id("nav.scan"), rect(x, by, bw, 32), model_.scanning ? "Scanning" : "Rescan",
                    widgets::Style::Secondary, [this] { model_.rescan(); }, !model_.scanning, icon::refresh);
    widgets::button(ui, make_id("nav.signout"), rect(x + bw + 8, by, bw, 32), "Sign out", widgets::Style::Ghost,
                    [this] { model_.sign_out(); });
}

void ShellScreen::paint(Ui& ui, const Rect& b) {
    ui.fill(b, theme::background);
    Rect content = {b.left + kSidebar, b.top, b.right, b.bottom};
    Page* page = search_page_ ? search_page_.get() : stack_.empty() ? nullptr : stack_.back().get();
    bool can_back = !search_page_ && stack_.size() > 1;
    if (page) {
        page->show_back = can_back;
        page->paint_page(ui, content);
    }

    if (can_back) {
        Rect btn = rect(content.left + 20, content.top + 16, 38, 38);
        bool hero = page && page->has_hero();
        ui.circle({btn.left + 19, btn.top + 19}, 19, hero ? Color{0, 0, 0, 0.45f} : Color{1, 1, 1, 0.06f});
        widgets::icon_button(ui, make_id("nav.back"), btn, icon::back, [this] { back(); });
    }

    paint_sidebar(ui, rect(b.left, b.top, kSidebar, height(b)));
}

}
