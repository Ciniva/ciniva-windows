#include "screens/shell_screen.h"

#include "app/app_model.h"
#include "ui/widgets.h"

namespace ciniva {

using namespace ui;

ShellScreen::ShellScreen(HWND hwnd, AppModel& model) : hwnd_(hwnd), model_(model) {}

void ShellScreen::paint(Ui& ui, const Rect& b) {
    ui.fill(b, theme::background);
    std::string who = model_.user ? model_.user->username : "";
    ui.text("Signed in as " + who, rect(40, 40, 600, 32), {Font::Title});
    ui.text(model_.client ? model_.client->base_url() : "", rect(40, 76, 600, 20), {Font::Body, theme::secondary});
    widgets::button(ui, make_id("shell.signout"), rect(40, 120, 120, 36), "Sign out", widgets::Style::Secondary,
                    [this] { model_.sign_out(); });
    widgets::button(ui, make_id("shell.scan"), rect(172, 120, 140, 36), "Rescan library", widgets::Style::Secondary,
                    [this] { model_.rescan(); }, !model_.scanning, icon::refresh);
}

}
