#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace ciniva::dispatch {

constexpr UINT WM_APP_INVOKE = WM_APP + 10;

void init(HWND ui_window);
void post(std::function<void()> fn);
void background(std::function<void()> fn);
void handle_invoke(LPARAM lp);

// Runs work on the thread pool and hands its result to done on the UI thread.
template <class Work, class Done>
void async(Work work, Done done) {
    background([work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<std::invoke_result_t<Work&>>(work());
        post([done = std::move(done), result]() mutable { done(std::move(*result)); });
    });
}

}
