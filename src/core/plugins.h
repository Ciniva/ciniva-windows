#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ciniva::plugins {

using json = nlohmann::json;

struct PageRef {
    std::string id;
    std::string title;
    std::string icon;
    bool nav = false;
};

struct ActionRef {
    std::string id;
    std::string title;
    std::string icon;
    std::vector<std::string> contexts;
};

struct Info {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string icon;
    bool online = true;
    std::vector<PageRef> pages;
    std::vector<ActionRef> actions;
    bool has_settings = false;
};

struct Nav {
    std::string page;
    json params = json::object();
};

struct Call {
    std::string method = "POST";
    std::string path;
    json body;
};

struct Action {
    std::string label;
    std::string icon;
    std::string tone;
    std::string confirm;
    std::optional<Call> call;
    std::optional<Nav> navigate;
};

struct Option {
    json value;
    std::string label;
};

struct Field {
    std::string name;
    std::string label;
    std::string kind = "text";
    json value;
    std::string placeholder;
    std::string help;
    bool required = false;
    std::vector<Option> options;

    json initial_value() const;
};

struct Badge {
    std::string text;
    std::string tone;
};

struct Item {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string image;
    std::optional<double> progress;
    std::vector<Badge> badges;
    std::vector<Action> actions;
    std::optional<Nav> link;
};

struct Form {
    std::string id;
    std::vector<Field> fields;
    std::string submit_label = "Save";
    std::optional<Call> submit_call;

    json body(const json& values) const;
    std::vector<std::string> missing(const json& values) const;
};

struct Block {
    std::string type;
    std::string text;
    std::string tone;
    std::vector<std::pair<std::string, std::string>> stats;
    std::vector<Action> actions;
    Form form;
    std::string id;
    std::vector<Item> items;
    std::string empty;
};

struct View {
    std::string title;
    std::string subtitle;
    double refresh = 0;
    std::vector<Block> blocks;
};

struct Result {
    std::string message;
    std::string tone;
    std::optional<View> view;
    std::optional<Nav> navigate;
    bool refresh = false;
};

std::vector<Info> parse_infos(const json& j);
View parse_view(const json& j);
Result parse_result(const json& j);
std::vector<Field> parse_fields(const json& j);
std::string to_text(const json& v);
std::string base64_encode(const std::string& data);

}
