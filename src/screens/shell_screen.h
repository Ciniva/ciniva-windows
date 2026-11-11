#pragma once

#include "screens/components.h"
#include "screens/screen.h"
#include "ui/text_field.h"

#include <memory>
#include <vector>

namespace ciniva {

class ShellScreen : public Screen {
public:
    enum class Section { Home, Movies, Shows, Settings, Plugin };

    ShellScreen(HWND hwnd, AppModel& model);
    void paint(ui::Ui& ui, const ui::Rect& bounds) override;
    bool on_key(WPARAM key) override;
    void on_show() override;
    void on_hide() override;

private:
    void select(Section s);
    void select_plugin(const std::string& plugin, const std::string& page);
    void push(std::unique_ptr<Page> page);
    bool back();
    void forget(Page* page);
    void paint_sidebar(ui::Ui& ui, const ui::Rect& r);
    void paint_account_menu(ui::Ui& ui, const ui::Rect& b);
    void search_changed();
    Nav nav();

    HWND hwnd_;
    AppModel& model_;
    Section section_ = Section::Home;
    std::vector<std::unique_ptr<Page>> stack_;
    std::unique_ptr<Page> search_page_;
    std::string query_;
    int search_generation_ = 0;
    ui::TextField search_;
    Page* last_page_ = nullptr;
    bool account_menu_ = false;
    std::string plugin_page_key_;
    ui::Rect account_rect_ = {};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}
