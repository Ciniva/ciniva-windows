#pragma once

#include "core/plugins.h"
#include "screens/components.h"

#include <memory>
#include <optional>
#include <string>

namespace ciniva {

std::unique_ptr<Page> make_plugin_page(Nav nav, std::string plugin_id, plugins::Nav page,
                                       std::optional<plugins::View> fixed = std::nullopt);
std::unique_ptr<Page> make_plugin_settings_page(Nav nav, std::string plugin_id, std::string name);

// Buttons for plugin actions offered for a movie, show or season; returns the x after the last button.
float plugin_action_buttons(ui::Ui& ui, Nav& nav, float x, float y, const std::string& kind, const nlohmann::json& context);

const wchar_t* plugin_glyph(const std::string& icon);

}
