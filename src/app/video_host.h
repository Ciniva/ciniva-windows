#pragma once

#include <windows.h>

namespace ciniva {

class VideoHost {
public:
    bool create(HWND parent);
    HWND hwnd() const { return hwnd_; }
    void set_bounds(const RECT& rc);
    void show(bool visible);
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

private:
    HWND hwnd_ = nullptr;
};

}
