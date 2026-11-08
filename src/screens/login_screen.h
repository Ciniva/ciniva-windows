#pragma once

#include "screens/screen.h"
#include "ui/text_field.h"

namespace ciniva {

class LoginScreen : public Screen {
public:
    LoginScreen(HWND hwnd, AppModel& model);
    void paint(ui::Ui& ui, const ui::Rect& bounds) override;
    void on_show() override;
    void on_hide() override;

private:
    void submit();

    AppModel& model_;
    ui::TextField server_;
    ui::TextField username_;
    ui::TextField password_;
};

}
