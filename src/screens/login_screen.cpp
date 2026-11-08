#include "screens/login_screen.h"

#include "app/app_model.h"
#include "core/settings.h"
#include "core/text.h"
#include "ui/widgets.h"

namespace ciniva {

using namespace ui;

LoginScreen::LoginScreen(HWND hwnd, AppModel& model) : model_(model) {
    auto tab = [this](TextField* prev, TextField* next) {
        return [prev, next](bool back) { (back ? prev : next)->focus(); };
    };
    server_.create(hwnd, {L"http://srv1.local:8484", false, [this] { username_.focus(); }, nullptr, nullptr,
                          tab(&password_, &username_)});
    username_.create(hwnd, {L"Username", false, [this] { password_.focus(); }, nullptr, nullptr, tab(&server_, &password_)});
    password_.create(hwnd, {L"Password", true, [this] { submit(); }, nullptr, nullptr, tab(&username_, &server_)});
}

void LoginScreen::on_show() {
    auto& s = Settings::get();
    server_.set_text(widen(s.server));
    username_.set_text(widen(s.username));
    password_.set_text(L"");
    (s.username.empty() ? username_ : password_).focus();
}

void LoginScreen::on_hide() {
    server_.hide();
    username_.hide();
    password_.hide();
}

void LoginScreen::submit() {
    if (model_.signing_in) return;
    model_.sign_in(server_.text_utf8(), username_.text_utf8(), password_.text_utf8());
}

void LoginScreen::paint(Ui& ui, const Rect& b) {
    ui.fill(b, theme::background);
    float w = 380;
    float x = (b.left + b.right - w) / 2;
    float h = 420 + (model_.error.empty() ? 0 : 56);
    float y = std::max(b.top + 24, (b.top + b.bottom - h) / 2);

    ui.text("Ciniva", rect(x, y, w, 44), {Font::Huge, theme::accent, Align::Center});
    y += 48;
    ui.text("Sign in to your server", rect(x, y, w, 22), {Font::Body, theme::secondary, Align::Center});
    y += 44;

    auto field = [&](const char* label, TextField& f) {
        ui.text(label, rect(x, y, w, 18), {Font::SmallBold, theme::secondary});
        y += 22;
        f.paint(ui, rect(x, y, w, 40), !model_.signing_in);
        y += 52;
    };
    field("Server", server_);
    field("Username", username_);
    field("Password", password_);

    if (!model_.error.empty()) {
        Rect er = rect(x, y, w, 46);
        ui.fill_round(er, 8, with_alpha(theme::error, 0.14f));
        ui.text(model_.error, inset(er, 12, 4), {Font::Small, theme::error, Align::Leading, true, true, 2});
        y += 56;
    }

    y += 4;
    std::string label = model_.signing_in ? "Signing in\xE2\x80\xA6" : "Sign in";
    widgets::button(ui, make_id("login.submit"), rect(x, y, w, 42), label, widgets::Style::Primary,
                    [this] { submit(); }, !model_.signing_in);
}

}
