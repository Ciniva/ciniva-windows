#pragma once

#include "screens/components.h"
#include "screens/screen.h"
#include "ui/text_field.h"

#include <memory>
#include <vector>

namespace ciniva {

class ShellScreen : public Screen {
public:
    enum class Section { Home, Movies, Shows };

    ShellScreen(HWND hwnd, AppModel& model);
    void paint(ui::Ui& ui, const ui::Rect& bounds) override;
    bool on_key(WPARAM key) override;
    void on_show() override;
    void on_hide() override;

private:
    void select(Section s);
    void push(std::unique_ptr<Page> page);
    bool back();
    void paint_sidebar(ui::Ui& ui, const ui::Rect& r);
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
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}
