#include "screens/plugin_page.h"

#include "app/dispatch.h"
#include "app/timing.h"
#include "core/text.h"
#include "ui/text_field.h"
#include "ui/widgets.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace ciniva {

using namespace ui;
using nlohmann::json;

namespace {

constexpr float kPad = 32;

Color tone_color(const std::string& tone) {
    if (tone == "error" || tone == "danger") return theme::error;
    if (tone == "success") return theme::success;
    if (tone == "warning") return {0.96f, 0.62f, 0.18f, 1};
    if (tone == "info") return {0.40f, 0.62f, 0.98f, 1};
    if (tone == "primary") return theme::accent;
    return theme::text;
}

Tone toast_tone(const std::string& tone) {
    return tone == "error" ? Tone::Error : tone == "success" ? Tone::Success : Tone::Info;
}

bool confirm(HWND hwnd, const std::string& text) {
    if (text.empty()) return true;
    return MessageBoxW(hwnd, widen(text).c_str(), L"Ciniva", MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}

std::optional<std::pair<std::string, std::string>> pick_file(HWND hwnd) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
    if (FAILED(dlg->Show(hwnd))) return std::nullopt;
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    std::wstring p = path;
    CoTaskMemFree(path);
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return std::pair{narrow(p.substr(p.find_last_of(L"\\/") + 1)), ss.str()};
}

void handle_result(Nav& nav, const std::string& plugin, const plugins::Result& r, const std::function<void(plugins::View)>& show,
                   const std::function<void()>& reload) {
    if (!r.message.empty()) nav.model.toast(r.message, toast_tone(r.tone));
    if (r.navigate) {
        nav.push(make_plugin_page(nav, plugin, *r.navigate));
    } else if (r.view) {
        if (show) show(*r.view);
        else nav.push(make_plugin_page(nav, plugin, {}, *r.view));
    } else if (r.refresh) {
        if (reload) reload();
        else nav.model.library_changed();
    }
}

class PluginPage : public Page {
public:
    PluginPage(Nav nav, std::string plugin, plugins::Nav page, std::optional<plugins::View> fixed, bool settings = false,
               std::string name = "")
        : Page(nav), plugin_(std::move(plugin)), page_(std::move(page)), settings_(settings), name_(std::move(name)) {
        if (fixed) {
            set_view(std::move(*fixed));
            fixed_ = true;
            loaded_ = true;
        }
    }

    ~PluginPage() override = default;

    std::string title() const override { return view_ ? view_->title : "page"; }

    void load() override {
        if (fixed_) return;
        auto alive = token();
        if (settings_) {
            model().call([p = plugin_](const Client& c) { return c.plugin_settings(p); }, [this, alive](Result<json> r) {
                if (alive.expired()) return;
                if (!r.ok()) {
                    error_ = r.error.message;
                } else {
                    plugins::View v;
                    v.title = name_ + " settings";
                    plugins::Block b;
                    b.type = "form";
                    b.form.fields = plugins::parse_fields(*r.value);
                    b.form.submit_label = "Save";
                    v.blocks.push_back(b);
                    set_view(std::move(v));
                    error_.clear();
                }
                loaded_ = true;
            });
            return;
        }
        model().call([p = plugin_, page = page_](const Client& c) { return c.plugin_page(p, page.page, page.params); },
                     [this, alive](Result<json> r) {
                         if (alive.expired()) return;
                         if (r.ok()) {
                             set_view(plugins::parse_view(*r.value));
                             error_.clear();
                         } else if (!view_) {
                             error_ = r.error.message;
                         }
                         loaded_ = true;
                         schedule_refresh();
                     });
    }

    void on_hide() override {
        for (auto& [k, f] : fields_) f->hide();
    }

protected:
    void set_view(plugins::View v) {
        bool same_shape = view_ && view_->blocks.size() == v.blocks.size();
        view_ = std::move(v);
        if (!same_shape) {
            fields_.clear();
            values_.clear();
        }
    }

    void schedule_refresh() {
        if (!view_ || view_->refresh <= 0 || refreshing_) return;
        refreshing_ = true;
        auto alive = token();
        int ms = static_cast<int>(std::max(1.0, view_->refresh) * 1000);
        dispatch::background([this, alive, ms] {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            dispatch::post([this, alive] {
                if (alive.expired()) return;
                refreshing_ = false;
                if (timing::now_seconds() - last_painted_ < 3 && !busy_) load();
                else schedule_refresh();
            });
        });
    }

    void run(const plugins::Action& a) {
        if (busy_ || !confirm(nav_.hwnd, a.confirm)) return;
        if (a.play) {
            play_live(std::vector<plugins::Channel>{plugins::Channel{a.play->stream, a.play->title, ""}}, 0);
            return;
        }
        if (a.navigate && !a.call) {
            nav_.push(make_plugin_page(nav_, plugin_, *a.navigate));
            return;
        }
        if (!a.call) return;
        call(*a.call, nullptr);
    }

    void play_live(std::vector<plugins::Channel> list, size_t index) {
        if (index >= list.size()) return;
        PlaybackRequest req;
        req.title = list[index].title;
        req.live = LiveRequest{plugin_, std::move(list), index};
        model().play(req);
    }

    void call(const plugins::Call& c, const json* body) {
        busy_ = true;
        auto alive = token();
        json b = body ? *body : c.body;
        bool has_body = body != nullptr;
        model().call([p = plugin_, c, b, has_body](const Client& cl) { return cl.plugin_call(p, c, has_body ? &b : nullptr); },
                     [this, alive](Result<json> r) {
                         if (alive.expired()) return;
                         busy_ = false;
                         if (!r.ok()) {
                             if (!r.error.unauthorized()) model().toast(r.error.message, Tone::Error);
                             return;
                         }
                         handle(plugins::parse_result(*r.value));
                     });
    }

    void handle(const plugins::Result& r) {
        auto alive = token();
        handle_result(nav_, plugin_, r, [this](plugins::View v) { set_view(std::move(v)); }, [this, alive] {
            if (!alive.expired()) load();
        });
    }

    void submit(size_t block, const plugins::Form& form) {
        json& vals = values_[block];
        if (!vals.is_object()) vals = json::object();
        auto missing = form.missing(vals);
        if (!missing.empty()) {
            std::string list;
            for (auto& m : missing) list += (list.empty() ? "" : ", ") + m;
            model().toast("Fill in " + list + ".", Tone::Error);
            return;
        }
        json body = form.body(vals);
        if (settings_) {
            busy_ = true;
            auto alive = token();
            model().call([p = plugin_, body](const Client& c) { return c.save_plugin_settings(p, body); }, [this, alive](Result<json> r) {
                if (alive.expired()) return;
                busy_ = false;
                if (r.ok()) {
                    model().toast("Settings saved", Tone::Success);
                    model().load_plugins();
                } else if (!r.error.unauthorized()) {
                    model().toast(r.error.message, Tone::Error);
                }
            });
            return;
        }
        if (form.submit_call) call(*form.submit_call, &body);
    }

    TextField& field(const std::string& key, const plugins::Field& f, json& vals) {
        auto it = fields_.find(key);
        if (it != fields_.end()) return *it->second;
        auto tf = std::make_unique<TextField>();
        std::string name = f.name;
        TextField* raw = tf.get();
        TextField::Options o;
        o.placeholder = widen(f.placeholder);
        o.password = f.kind == "password";
        o.on_change = [raw, &vals, name] { vals[name] = raw->text_utf8(); };
        o.on_submit = [] {};
        tf->create(nav_.hwnd, o);
        json init = f.initial_value();
        tf->set_text(widen(init.is_string() ? init.get<std::string>() : plugins::to_text(init)));
        if (!vals.contains(name)) vals[name] = init.is_null() ? json("") : json(plugins::to_text(init));
        return *fields_.emplace(key, std::move(tf)).first->second;
    }

    float action_row(Ui& ui, std::string_view key, float x, float y, float max_w, const std::vector<plugins::Action>& actions,
                     bool compact) {
        float h = compact ? 30.0f : 36.0f;
        float cx = x;
        for (size_t i = 0; i < actions.size(); ++i) {
            const auto& a = actions[i];
            const wchar_t* g = a.icon.empty() ? nullptr : plugin_glyph(a.icon);
            float w = widgets::button_width(ui, a.label, g != nullptr) - (compact ? 8 : 0);
            if (cx + w > x + max_w && cx > x) {
                cx = x;
                y += h + 8;
            }
            auto style = a.tone == "primary" ? widgets::Style::Primary : a.tone == "danger" ? widgets::Style::Danger
                                                                                            : widgets::Style::Secondary;
            plugins::Action copy = a;
            widgets::button(ui, make_id(key, static_cast<long long>(i)), rect(cx, y, w, h), a.label, style,
                            [this, copy] { run(copy); }, !busy_, g);
            cx += w + 8;
        }
        return actions.empty() ? 0 : y + h - (y - 0) + 0;
    }

    float paint_content(Ui& ui, const Rect& r, float y) override {
        last_painted_ = ui.now();
        if (!view_) return y;
        float start = y;
        float x = r.left + kPad + (show_back ? 48 : 0);
        float w = std::min(r.right - kPad - x, 980.0f);
        y += 28;
        ui.text(view_->title, rect(x, y, w, 36), {Font::Title});
        y += 42;
        if (!view_->subtitle.empty()) {
            ui.text(view_->subtitle, rect(x, y, w, 20), {Font::Body, theme::secondary});
            y += 28;
        }
        x = r.left + kPad;
        w = std::min(r.right - kPad - x, 980.0f);
        y += 12;
        for (size_t bi = 0; bi < view_->blocks.size(); ++bi) {
            const auto& b = view_->blocks[bi];
            std::string key = "pl" + std::to_string(bi);
            if (b.type == "text") {
                auto sz = ui.measure(b.text, Font::Body, w, true);
                ui.text(b.text, rect(x, y, w, sz.height + 4), {Font::Body, b.tone.empty() ? theme::text : tone_color(b.tone), Align::Leading, true, false});
                y += sz.height + 16;
            } else if (b.type == "stats") {
                float tw = 180, th = 70, cx = x;
                for (auto& [label, value] : b.stats) {
                    if (cx + tw > x + w && cx > x) {
                        cx = x;
                        y += th + 10;
                    }
                    Rect t = rect(cx, y, tw, th);
                    ui.fill_round(t, 10, theme::surface);
                    ui.text(value, rect(cx + 14, y + 10, tw - 28, 28), {Font::Heading});
                    ui.text(label, rect(cx + 14, y + 42, tw - 28, 18), {Font::Small, theme::secondary});
                    cx += tw + 10;
                }
                y += th + 20;
            } else if (b.type == "actions") {
                float before = y;
                float cx = x;
                for (size_t i = 0; i < b.actions.size(); ++i) {
                    const auto& a = b.actions[i];
                    const wchar_t* g = a.icon.empty() ? nullptr : plugin_glyph(a.icon);
                    float bw = widgets::button_width(ui, a.label, g != nullptr);
                    if (cx + bw > x + w && cx > x) {
                        cx = x;
                        y += 46;
                    }
                    auto style = a.tone == "primary" ? widgets::Style::Primary : a.tone == "danger" ? widgets::Style::Danger
                                                                                                    : widgets::Style::Secondary;
                    plugins::Action copy = a;
                    widgets::button(ui, make_id(key, static_cast<long long>(i)), rect(cx, y, bw, 38), a.label, style,
                                    [this, copy] { run(copy); }, !busy_, g);
                    cx += bw + 10;
                }
                (void)before;
                y += 38 + 20;
            } else if (b.type == "form") {
                y += paint_form(ui, key, bi, b.form, x, y, std::min(w, 620.0f)) + 20;
            } else if (b.type == "list") {
                y += paint_list(ui, key, b, x, y, w) + 20;
            }
        }
        return y + 24 - start + start;
    }

    float paint_form(Ui& ui, const std::string& key, size_t bi, const plugins::Form& form, float x, float y, float w) {
        float y0 = y;
        json& vals = values_[bi];
        if (!vals.is_object()) vals = json::object();
        for (auto& f : form.fields) {
            std::string fkey = key + "." + f.name;
            ui.text(f.label + (f.required ? " *" : ""), rect(x, y, w, 18), {Font::SmallBold, theme::secondary});
            y += 24;
            if (!vals.contains(f.name) && f.kind != "text" && f.kind != "password" && f.kind != "number" && f.kind != "textarea")
                vals[f.name] = f.initial_value();
            if (f.kind == "toggle") {
                bool on = vals[f.name].is_boolean() && vals[f.name].get<bool>();
                Rect t = rect(x, y, 46, 26);
                auto st = ui.region(make_id(fkey), t, [&vals, name = f.name, on] { vals[name] = !on; });
                ui.fill_round(t, 13, on ? theme::accent : st.hover ? Color{1, 1, 1, 0.24f} : Color{1, 1, 1, 0.16f});
                ui.circle({on ? t.right - 13 : t.left + 13, t.top + 13}, 9, on ? theme::accent_text : Color{1, 1, 1, 0.9f});
                y += 34;
            } else if (f.kind == "select") {
                float cx = x;
                for (size_t i = 0; i < f.options.size(); ++i) {
                    const auto& o = f.options[i];
                    float pw = ui.measure(o.label, Font::SmallBold).width + 26;
                    if (cx + pw > x + w && cx > x) {
                        cx = x;
                        y += 38;
                    }
                    Rect p = rect(cx, y, pw, 32);
                    bool on = vals[f.name] == o.value;
                    auto st = ui.region(make_id(fkey, static_cast<long long>(i)), p, [&vals, name = f.name, v = o.value] { vals[name] = v; });
                    ui.fill_round(p, 16, on ? theme::accent : st.hover ? theme::surface_hover : theme::surface);
                    ui.text(o.label, p, {Font::SmallBold, on ? theme::accent_text : theme::text, Align::Center});
                    if (st.focused) ui.focus_ring(p, 16);
                    cx += pw + 6;
                }
                y += 42;
            } else if (f.kind == "file") {
                std::string label = vals[f.name].is_object() ? plugins::to_text(vals[f.name].value("name", json(""))) : "No file chosen";
                widgets::button(ui, make_id(fkey), rect(x, y, 140, 36), "Choose file\xE2\x80\xA6", widgets::Style::Secondary,
                                [this, &vals, name = f.name] {
                                    if (auto file = pick_file(nav_.hwnd))
                                        vals[name] = {{"name", file->first}, {"data_base64", plugins::base64_encode(file->second)}};
                                });
                ui.text(label, rect(x + 152, y, w - 152, 36), {Font::Small, theme::secondary});
                y += 44;
            } else {
                field(fkey, f, vals).paint(ui, rect(x, y, w, 38), !busy_);
                y += 46;
            }
            if (!f.help.empty()) {
                auto sz = ui.measure(f.help, Font::Small, w, true);
                ui.text(f.help, rect(x, y - 4, w, sz.height + 4), {Font::Small, theme::tertiary, Align::Leading, true, false});
                y += sz.height + 6;
            }
            y += 6;
        }
        plugins::Form copy = form;
        float bw = std::max(120.0f, widgets::button_width(ui, form.submit_label));
        widgets::button(ui, make_id(key + ".submit"), rect(x, y, bw, 40), busy_ ? "Working\xE2\x80\xA6" : form.submit_label,
                        widgets::Style::Primary, [this, bi, copy] { submit(bi, copy); }, !busy_);
        y += 40;
        return y - y0;
    }

    float paint_list(Ui& ui, const std::string& key, const plugins::Block& b, float x, float y, float w) {
        float y0 = y;
        if (b.items.empty()) {
            ui.text(b.empty.empty() ? "Nothing here." : b.empty, rect(x, y, w, 22), {Font::Body, theme::secondary});
            return 30;
        }
        auto channels = plugins::channels(b);
        size_t channel = 0;
        for (size_t i = 0; i < b.items.size(); ++i) {
            const auto& it = b.items[i];
            bool live = it.play.has_value();
            bool has_image = !it.image.empty() && !live;
            float head = live ? 76.0f : has_image ? 92.0f : 64.0f;
            float h = head;
            if (it.progress) h += 10;
            if (!it.actions.empty()) h += 40;
            Rect row = rect(x, y, w, h);
            std::function<void()> open;
            if (live) {
                size_t index = channel++;
                open = [this, channels, index] { play_live(channels, index); };
            } else if (it.link) {
                plugins::Nav link = *it.link;
                open = [this, link] { nav_.push(make_plugin_page(nav_, plugin_, link)); };
            }
            bool clickable = static_cast<bool>(open);
            auto st = ui.region(make_id(key + ".row", static_cast<long long>(i)), row, open, clickable);
            ui.fill_round(row, 10, st.hover && clickable ? theme::surface_hover : theme::surface);
            if (st.focused) ui.focus_ring(row, 10);
            float tx = x + 16;
            if (live) {
                auto* c = client();
                Rect logo = rect(x + 12, y + 12, 72, 52);
                widgets::channel_logo(ui, c ? c->image_url(it.image, "w185") : std::nullopt, logo);
                tx = logo.right + 14;
            } else if (has_image) {
                auto* c = client();
                Rect img = rect(x + 12, y + 12, 48, 68);
                ui.image(c ? c->image_url(it.image, "w185") : std::nullopt, img, 6, static_cast<int>(48 * ui.renderer().scale()));
                tx = img.right + 14;
            }
            float bx = x + w - 14 - (clickable ? 24.0f : 0.0f);
            for (auto bd = it.badges.rbegin(); bd != it.badges.rend(); ++bd) {
                float bw = widgets::chip_width(ui, bd->text);
                bx -= bw;
                Color c = bd->tone.empty() ? theme::secondary : tone_color(bd->tone);
                widgets::chip(ui, rect(bx, y + 14, bw, 22), bd->text, with_alpha(c, 0.16f), c);
                bx -= 6;
            }
            if (clickable)
                widgets::glyph(ui, live ? icon::play : icon::chevron_right, rect(x + w - 30, y, 20, head), live ? theme::accent : theme::tertiary,
                               Font::IconSmall);
            float tw = bx - tx - 8;
            ui.text(it.title, rect(tx, y + 12, tw, 22), {Font::BodyBold});
            if (!it.subtitle.empty()) ui.text(it.subtitle, rect(tx, y + 34, tw, 34), {Font::Small, theme::secondary, Align::Leading, true, false, 2});
            float cy = y + (live ? 70.0f : has_image ? 84.0f : 58.0f);
            if (it.progress) {
                widgets::progress_bar(ui, rect(tx, cy, x + w - 16 - tx, 5), *it.progress);
                cy += 12;
            }
            if (!it.actions.empty()) action_row(ui, key + ".a" + std::to_string(i), tx, cy + 2, x + w - 16 - tx, it.actions, true);
            y += h + 8;
        }
        return y - y0;
    }

private:
    std::string plugin_;
    plugins::Nav page_;
    bool settings_ = false;
    std::string name_;
    bool fixed_ = false;
    std::optional<plugins::View> view_;
    std::map<size_t, json> values_;
    std::map<std::string, std::unique_ptr<TextField>> fields_;
    bool busy_ = false;
    bool refreshing_ = false;
    double last_painted_ = 0;
};

}

const wchar_t* plugin_glyph(const std::string& icon) {
    std::string i = icon;
    for (auto& c : i) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::map<std::string, const wchar_t*> map = {
        {"download", L""}, {"upload", L""}, {"search", L""}, {"play", L""}, {"pause", L""},
        {"stop", L""}, {"trash", L""}, {"delete", L""}, {"remove", L""}, {"refresh", L""},
        {"reload", L""}, {"check", L""}, {"done", L""}, {"x", L""}, {"close", L""},
        {"cancel", L""}, {"plus", L""}, {"add", L""}, {"gear", L""}, {"settings", L""},
        {"list", L""}, {"folder", L""}, {"link", L""}, {"star", L""}, {"info", L""},
        {"warning", L""}, {"alert", L""}, {"film", L""}, {"movie", L""}, {"movies", L""},
        {"tv", L""}, {"show", L""}, {"shows", L""}, {"wrench", L""}, {"fix", L""},
        {"subtitles", L""}, {"captions", L""}, {"music", L""}, {"audio", L""},
        {"calendar", L""}, {"rss", L""}, {"feed", L""}};
    auto it = map.find(i);
    return it == map.end() ? L"" : it->second;
}

std::unique_ptr<Page> make_plugin_page(Nav nav, std::string plugin_id, plugins::Nav page, std::optional<plugins::View> fixed) {
    return std::make_unique<PluginPage>(nav, std::move(plugin_id), std::move(page), std::move(fixed));
}

std::unique_ptr<Page> make_plugin_settings_page(Nav nav, std::string plugin_id, std::string name) {
    return std::make_unique<PluginPage>(nav, std::move(plugin_id), plugins::Nav{}, std::nullopt, true, std::move(name));
}

float plugin_action_buttons(Ui& ui, Nav& nav, float x, float y, const std::string& kind, const json& context) {
    float cx = x;
    for (auto& p : nav.model.plugin_infos) {
        if (!p.online) continue;
        for (auto& a : p.actions) {
            if (std::find(a.contexts.begin(), a.contexts.end(), kind) == a.contexts.end()) continue;
            const wchar_t* g = plugin_glyph(a.icon.empty() ? "search" : a.icon);
            float w = widgets::button_width(ui, a.title, true);
            std::string plugin = p.id, action = a.id;
            Nav* n = &nav;
            widgets::button(ui, make_id("pa." + plugin + "." + action + "." + kind), rect(cx, y, w, 40), a.title,
                            widgets::Style::Secondary, [n, plugin, action, context] {
                                Nav copy = *n;
                                n->model.call([plugin, action, context](const Client& c) { return c.plugin_action(plugin, action, context); },
                                              [copy, plugin](Result<json> r) mutable {
                                                  if (!r.ok()) {
                                                      if (!r.error.unauthorized()) copy.model.toast(r.error.message, Tone::Error);
                                                      return;
                                                  }
                                                  handle_result(copy, plugin, plugins::parse_result(*r.value), nullptr, nullptr);
                                              });
                            }, true, g);
            cx += w + 10;
        }
    }
    return cx;
}

}
