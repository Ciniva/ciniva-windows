#pragma once

#include "screens/screen.h"

namespace ciniva {

class ShellScreen : public Screen {
public:
    ShellScreen(HWND hwnd, AppModel& model);
    void paint(ui::Ui& ui, const ui::Rect& bounds) override;

private:
    HWND hwnd_;
    AppModel& model_;
};

}
