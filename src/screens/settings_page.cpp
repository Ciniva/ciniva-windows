#include "screens/pages.h"

#include "core/settings.h"
#include "core/text.h"
#include "screens/plugin_page.h"
#include "ui/text_field.h"
#include "ui/widgets.h"
#include "version.h"

#include <algorithm>

namespace ciniva {

using namespace ui;

namespace {

constexpr float kPad = 32;
const int kSeekChoices[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600};

std::string seek_label(int s) {
    if (s < 60) return std::to_string(s) + " seconds";
    if (s % 60 == 0) return std::to_string(s / 60) + " min";
    return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
}

class SettingsPage : public Page {
public:
    SettingsPage(Nav nav, HWND hwnd) : Page(nav) {
        auto commit = [this] { save_languages(); };
        audio_.create(hwnd, {L"e.g. eng, jpn", false, [this] { SetFocus(GetParent(GetFocus())); }, commit, nullptr, nullptr});
        subs_.create(hwnd, {L"e.g. eng", false, [this] { SetFocus(GetParent(GetFocus())); }, commit, nullptr, nullptr});
        auto& s = Settings::get();
        audio_.set_text(widen(s.audio_lang));
        subs_.set_text(widen(s.sub_lang));
        loaded_ = true;
    }

    std::string title() const override { return "Settings"; }
    void load() override {}
    void on_hide() override {
        audio_.hide();
        subs_.hide();
    }

protected:
    void changed() {
        Settings::get().save();
        if (model().on_settings_changed) model().on_settings_changed();
    }

    void save_languages() {
        auto& s = Settings::get();
        std::string a = trim(audio_.text_utf8()), b = trim(subs_.text_utf8());
        if (a == s.audio_lang && b == s.sub_lang) return;
        s.audio_lang = a;
        s.sub_lang = b;
        changed();
    }

    float section(Ui& ui, const Rect& r, float y, const char* title) {
        ui.text(title, rect(r.left + kPad, y, 600, 26), {Font::Heading});
        return 38;
    }

    float row(Ui& ui, const Rect& r, float y, const std::string& label, const std::string& detail, float control_h,
              const std::function<void(float x, float cy)>& control) {
        float w = std::min(width(r) - kPad * 2, 860.0f);
        float lx = r.left + kPad;
        float dh = detail.empty() ? 0 : ui.measure(detail, Font::Small, w * 0.55f, true).height;
        float h = std::max(control_h, 22 + dh) + 24;
        Rect box = rect(lx, y, w, h);
        ui.fill_round(box, 10, theme::surface);
        ui.text(label, rect(lx + 18, detail.empty() ? y + (h - 22) / 2 : y + 12, w * 0.55f, 22), {Font::BodyBold});
        if (!detail.empty())
            ui.text(detail, rect(lx + 18, y + 34, w * 0.55f, dh + 4), {Font::Small, theme::secondary, Align::Leading, true, false});
        control(lx + w - 18, y + h / 2);
        return h + 8;
    }

    void toggle(Ui& ui, Id id, float right, float cy, bool on, std::function<void()> fn) {
        Rect r = rect(right - 46, cy - 13, 46, 26);
        auto st = ui.region(id, r, std::move(fn));
        ui.fill_round(r, 13, on ? theme::accent : st.hover ? Color{1, 1, 1, 0.24f} : Color{1, 1, 1, 0.16f});
        ui.circle({on ? r.right - 13 : r.left + 13, cy}, 9, on ? theme::accent_text : Color{1, 1, 1, 0.9f});
        if (st.focused) ui.focus_ring(r, 13);
    }

    void choices(Ui& ui, std::string_view key, float right, float cy, const std::vector<std::pair<int, std::string>>& options,
                 int selected, const std::function<void(int)>& fn) {
        float total = 0;
        for (auto& [v, l] : options) total += ui.measure(l, Font::SmallBold).width + 24;
        float x = right - total - 4;
        Rect bg = rect(x - 3, cy - 17, total + 6 + 4, 34);
        ui.fill_round(bg, 9, {0, 0, 0, 0.25f});
        for (auto& [v, l] : options) {
            float w = ui.measure(l, Font::SmallBold).width + 24;
            Rect r = rect(x, cy - 14, w, 28);
            bool on = v == selected;
            int value = v;
            auto st = ui.region(make_id(key, v), r, [fn, value] { fn(value); });
            if (on || st.hover) ui.fill_round(r, 7, on ? theme::accent : Color{1, 1, 1, 0.08f});
            ui.text(l, r, {Font::SmallBold, on ? theme::accent_text : theme::text, Align::Center});
            if (st.focused) ui.focus_ring(r, 7);
            x += w + 1;
        }
    }

    void stepper(Ui& ui, std::string_view key, float right, float cy, int& value) {
        auto it = std::find(std::begin(kSeekChoices), std::end(kSeekChoices), value);
        size_t i = it == std::end(kSeekChoices) ? 1 : static_cast<size_t>(it - std::begin(kSeekChoices));
        size_t n = std::size(kSeekChoices);
        float w = 200;
        Rect r = rect(right - w, cy - 17, w, 34);
        ui.fill_round(r, 9, {0, 0, 0, 0.25f});
        ui.text(seek_label(value), r, {Font::BodyBold, theme::text, Align::Center});
        widgets::icon_button(ui, make_id(key, 1), rect(r.left + 2, r.top + 2, 30, 30), icon::chevron_left, [this, &value, i] {
            if (i > 0) {
                value = kSeekChoices[i - 1];
                changed();
            }
        }, false, Font::IconSmall);
        widgets::icon_button(ui, make_id(key, 2), rect(r.right - 32, r.top + 2, 30, 30), icon::chevron_right, [this, &value, i, n] {
            if (i + 1 < n) {
                value = kSeekChoices[i + 1];
                changed();
            }
        }, false, Font::IconSmall);
    }

    float paint_content(Ui& ui, const Rect& r, float y) override {
        auto& s = Settings::get();
        y += 28;
        ui.text("Settings", rect(r.left + kPad, y, 600, 36), {Font::Title});
        y += 64;

        y += section(ui, r, y, "Playback");
        y += row(ui, r, y, "HDR output",
                 "Switch the display to HDR10 for HDR and Dolby Vision titles when Windows HDR is on. Off tone-maps everything to SDR.",
                 26, [&](float x, float cy) { toggle(ui, make_id("set.hdr"), x, cy, s.hdr, [this, &s] { s.hdr = !s.hdr; changed(); }); });
        y += row(ui, r, y, "Display peak brightness", "Used for tone mapping when the display doesn't report its peak.", 34,
                 [&](float x, float cy) {
                     choices(ui, "set.peak", x, cy, {{0, "Auto"}, {400, "400"}, {600, "600"}, {1000, "1000"}, {1500, "1500"}},
                             s.peak_nits, [this, &s](int v) { s.peak_nits = v; changed(); });
                 });
        y += row(ui, r, y, "Audio passthrough",
                 "Send Dolby Digital, DTS, TrueHD and DTS-HD bitstreams untouched to an AV receiver over HDMI or S/PDIF.", 26,
                 [&](float x, float cy) {
                     toggle(ui, make_id("set.pass"), x, cy, s.passthrough, [this, &s] { s.passthrough = !s.passthrough; changed(); });
                 });
        y += row(ui, r, y, "Play the next episode automatically", "Counts down for 10 seconds at the credits or the end.", 26,
                 [&](float x, float cy) {
                     toggle(ui, make_id("set.auto"), x, cy, s.autoplay_next, [this, &s] { s.autoplay_next = !s.autoplay_next; changed(); });
                 });
        y += row(ui, r, y, "Preferred audio languages", "ISO codes in order of preference, separated by commas.", 36,
                 [&](float x, float cy) { audio_.paint(ui, rect(x - 220, cy - 18, 220, 36)); });
        y += row(ui, r, y, "Preferred subtitle languages", "Leave empty to keep subtitles off unless the file forces them.", 36,
                 [&](float x, float cy) { subs_.paint(ui, rect(x - 220, cy - 18, 220, 36)); });
        y += 20;

        y += section(ui, r, y, "Seeking");
        y += row(ui, r, y, "\xE2\x86\x90 / \xE2\x86\x92", "", 34, [&](float x, float cy) { stepper(ui, "set.seek1", x, cy, s.seek_short); });
        y += row(ui, r, y, "Shift + \xE2\x86\x90 / \xE2\x86\x92", "", 34, [&](float x, float cy) { stepper(ui, "set.seek2", x, cy, s.seek_medium); });
        y += row(ui, r, y, "Ctrl + \xE2\x86\x90 / \xE2\x86\x92", "", 34, [&](float x, float cy) { stepper(ui, "set.seek3", x, cy, s.seek_long); });
        widgets::button(ui, make_id("set.seekreset"), rect(r.left + kPad, y + 4, 230, 34), "Reset to 10 s / 30 s / 60 s",
                        widgets::Style::Ghost, [this, &s] {
                            s.seek_short = 10;
                            s.seek_medium = 30;
                            s.seek_long = 60;
                            changed();
                        });
        y += 60;

        y += section(ui, r, y, "Updates");
        y += row(ui, r, y, "Check for updates automatically", "At launch and every 6 hours. Updates are verified with Ciniva's signing key.", 26,
                 [&](float x, float cy) {
                     toggle(ui, make_id("set.upd"), x, cy, s.auto_update, [this, &s] { s.auto_update = !s.auto_update; changed(); });
                 });
        y += row(ui, r, y, std::string("Ciniva ") + CINIVA_VERSION, model().update_status, 34, [&](float x, float cy) {
            float w = widgets::button_width(ui, "Check now");
            widgets::button(ui, make_id("set.updnow"), rect(x - w, cy - 17, w, 34), "Check now", widgets::Style::Secondary,
                            [this] { if (model().on_check_updates) model().on_check_updates(); }, model().on_check_updates != nullptr);
        });
        y += 20;

        y += section(ui, r, y, "Account");
        std::string who = model().user ? model().user->username + (model().user->admin ? " (administrator)" : "") : "";
        y += row(ui, r, y, who, model().client ? model().client->base_url() : "", 34, [&](float x, float cy) {
            float w = widgets::button_width(ui, "Sign out");
            widgets::button(ui, make_id("set.signout"), rect(x - w, cy - 17, w, 34), "Sign out", widgets::Style::Danger,
                            [this] { model().sign_out(); });
        });
        y += row(ui, r, y, "Library", model().scanning ? "A scan is running." : "Look for new and removed files on the server.", 34,
                 [&](float x, float cy) {
                     float w = widgets::button_width(ui, "Rescan library", true);
                     widgets::button(ui, make_id("set.scan"), rect(x - w, cy - 17, w, 34), "Rescan library", widgets::Style::Secondary,
                                     [this] { model().rescan(); }, !model().scanning, icon::refresh);
                 });
        size_t plugins = model().plugins.is_array() ? model().plugins.size() : 0;
        y += row(ui, r, y, "Plugins", std::to_string(plugins) + (plugins == 1 ? " plugin installed on the server." : " plugins installed on the server."), 34,
                 [&](float x, float cy) {
                     float w = widgets::button_width(ui, "Reload");
                     widgets::button(ui, make_id("set.plugins"), rect(x - w, cy - 17, w, 34), "Reload", widgets::Style::Secondary,
                                     [this] { model().load_plugins(); model().toast("Plugins reloaded"); });
                 });
        bool admin = model().user && model().user->admin;
        for (auto& p : model().plugin_infos) {
            std::string detail = p.description.empty() ? "" : p.description;
            if (!p.online) detail = "Offline. " + detail;
            std::string label = p.name + (p.version.empty() ? "" : " " + p.version);
            y += row(ui, r, y, label, detail, 34, [&](float x, float cy) {
                if (!admin || !p.has_settings) return;
                float w = widgets::button_width(ui, "Settings", true);
                std::string id = p.id, name = p.name;
                widgets::button(ui, make_id("set.plugin." + id), rect(x - w, cy - 17, w, 34), "Settings", widgets::Style::Secondary,
                                [this, id, name] { nav_.push(make_plugin_settings_page(nav_, id, name)); }, true, icon::settings);
            });
        }
        return y + 40;
    }

private:
    TextField audio_;
    TextField subs_;
};

}

std::unique_ptr<Page> make_settings_page(Nav nav, HWND hwnd) { return std::make_unique<SettingsPage>(nav, hwnd); }

}
