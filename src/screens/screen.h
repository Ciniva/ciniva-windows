#pragma once

#include "ui/ui.h"

namespace ciniva {

class AppModel;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void paint(ui::Ui& ui, const ui::Rect& bounds) = 0;
    virtual bool on_key(WPARAM) { return false; }
    virtual void on_show() {}
    virtual void on_hide() {}
};

}
