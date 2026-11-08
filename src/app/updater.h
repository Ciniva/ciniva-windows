#pragma once

#include "core/update.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ciniva {

class Updater {
public:
    enum class Phase { Idle, Checking, UpToDate, Available, Installing, Failed };

    Updater(std::function<void()> changed, std::function<void()> quit) : changed_(std::move(changed)), quit_(std::move(quit)) {}
    ~Updater() { *alive_ = false; }

    void start_automatic_checks();
    void check(bool user_initiated);
    void install();

    Phase phase() const { return phase_; }
    const std::optional<update::Release>& available() const { return available_; }
    std::string status() const;

    static void remove_leftovers();
    // Copies the running build to %LOCALAPPDATA%\Programs\Ciniva, adds a Start menu shortcut and launches it.
    static bool install_per_user(std::wstring* error);

private:
    void schedule();

    std::function<void()> changed_;
    std::function<void()> quit_;
    Phase phase_ = Phase::Idle;
    std::optional<update::Release> available_;
    std::string error_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}
