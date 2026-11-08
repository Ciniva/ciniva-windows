#include "core/plugins.h"

#include "core/text.h"

#include <cmath>
#include <cstdio>

namespace ciniva::plugins {

namespace {

std::string str(const json& j, const char* key, std::string fallback = "") {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    return to_text(*it);
}

bool flag(const json& j, const char* key, bool fallback = false) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

const json& arr(const json& j, const char* key) {
    static const json empty = json::array();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it != j.end() && it->is_array() ? *it : empty;
}

std::optional<double> number(const json& j, const char* key) {
    if (!j.is_object()) return std::nullopt;
    auto it = j.find(key);
    if (it == j.end()) return std::nullopt;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
        }
    }
    return std::nullopt;
}

std::optional<Nav> parse_nav(const json& j) {
    if (!j.is_object() || !j.contains("page")) return std::nullopt;
    Nav n;
    n.page = str(j, "page");
    if (j.contains("params") && j["params"].is_object()) n.params = j["params"];
    return n;
}

std::optional<Call> parse_call(const json& j) {
    if (!j.is_object() || !j.contains("path")) return std::nullopt;
    Call c;
    c.method = str(j, "method", "POST");
    for (auto& ch : c.method) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    c.path = str(j, "path");
    if (j.contains("body") && j["body"].is_object()) c.body = j["body"];
    return c;
}

Action parse_action(const json& j) {
    Action a;
    a.label = str(j, "label");
    a.icon = str(j, "icon");
    a.tone = str(j, "tone");
    a.confirm = str(j, "confirm");
    if (j.is_object() && j.contains("call")) a.call = parse_call(j["call"]);
    if (j.is_object() && j.contains("navigate")) a.navigate = parse_nav(j["navigate"]);
    return a;
}

Field parse_field(const json& j) {
    Field f;
    f.name = str(j, "name");
    f.label = str(j, "label", f.name);
    f.kind = str(j, "kind", "text");
    if (j.is_object() && j.contains("value")) f.value = j["value"];
    f.placeholder = str(j, "placeholder");
    f.help = str(j, "help");
    f.required = flag(j, "required");
    for (auto& o : arr(j, "options")) {
        Option opt;
        opt.value = o.is_object() && o.contains("value") ? o["value"] : o;
        opt.label = o.is_object() ? str(o, "label", to_text(opt.value)) : to_text(o);
        f.options.push_back(opt);
    }
    return f;
}

}

std::string to_text(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return "";
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) {
        double d = v.get<double>();
        if (std::floor(d) == d && std::fabs(d) < 1e15) return std::to_string(static_cast<long long>(d));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", d);
        return buf;
    }
    return v.dump();
}

json Field::initial_value() const {
    if (!value.is_null()) return value;
    if (kind == "toggle") return false;
    if (kind == "select") return options.empty() ? json() : options.front().value;
    if (kind == "file") return json();
    return "";
}

json Form::body(const json& values) const {
    json out = submit_call && submit_call->body.is_object() ? submit_call->body : json::object();
    for (auto& f : fields) {
        json v = values.contains(f.name) ? values[f.name] : f.initial_value();
        if (f.kind == "number" && v.is_string()) {
            std::string s = trim(v.get<std::string>());
            try {
                size_t used = 0;
                double d = std::stod(s, &used);
                out[f.name] = used == s.size() ? json(d) : json(s);
            } catch (...) {
                out[f.name] = s.empty() ? json() : json(s);
            }
        } else {
            out[f.name] = v;
        }
    }
    return out;
}

std::vector<std::string> Form::missing(const json& values) const {
    std::vector<std::string> out;
    for (auto& f : fields) {
        if (!f.required) continue;
        json v = values.contains(f.name) ? values[f.name] : f.initial_value();
        if (v.is_null() || (v.is_string() && trim(v.get<std::string>()).empty())) out.push_back(f.label);
    }
    return out;
}

std::vector<Info> parse_infos(const json& j) {
    std::vector<Info> out;
    if (!j.is_array()) return out;
    for (auto& p : j) {
        if (!p.is_object() || !p.contains("id")) continue;
        Info i;
        i.id = str(p, "id");
        i.name = str(p, "name", i.id);
        i.version = str(p, "version");
        i.description = str(p, "description");
        i.icon = str(p, "icon");
        i.online = flag(p, "online", true);
        i.has_settings = flag(p, "has_settings");
        for (auto& pg : arr(p, "pages")) i.pages.push_back({str(pg, "id"), str(pg, "title", str(pg, "id")), str(pg, "icon"), flag(pg, "nav")});
        for (auto& a : arr(p, "actions")) {
            ActionRef r{str(a, "id"), str(a, "title", str(a, "id")), str(a, "icon"), {}};
            for (auto& c : arr(a, "contexts")) r.contexts.push_back(to_text(c));
            i.actions.push_back(r);
        }
        out.push_back(i);
    }
    return out;
}

View parse_view(const json& j) {
    View v;
    v.title = str(j, "title");
    v.subtitle = str(j, "subtitle");
    v.refresh = number(j, "refresh").value_or(0);
    for (auto& b : arr(j, "blocks")) {
        Block block;
        block.type = str(b, "type");
        block.id = str(b, "id");
        if (block.type == "text") {
            block.text = str(b, "text");
            block.tone = str(b, "tone");
        } else if (block.type == "stats") {
            for (auto& s : arr(b, "items")) block.stats.push_back({str(s, "label"), s.contains("value") ? to_text(s["value"]) : ""});
        } else if (block.type == "actions") {
            for (auto& a : arr(b, "items")) block.actions.push_back(parse_action(a));
        } else if (block.type == "form") {
            block.form.id = block.id;
            for (auto& f : arr(b, "fields")) block.form.fields.push_back(parse_field(f));
            if (b.contains("submit") && b["submit"].is_object()) {
                block.form.submit_label = str(b["submit"], "label", "Save");
                if (b["submit"].contains("call")) block.form.submit_call = parse_call(b["submit"]["call"]);
            }
        } else if (block.type == "list") {
            block.empty = str(b, "empty");
            for (auto& it : arr(b, "items")) {
                Item item;
                item.title = str(it, "title");
                item.id = str(it, "id", item.title);
                item.subtitle = str(it, "subtitle");
                item.image = str(it, "image");
                item.progress = number(it, "progress");
                for (auto& bd : arr(it, "badges")) item.badges.push_back({str(bd, "text"), str(bd, "tone")});
                for (auto& a : arr(it, "actions")) item.actions.push_back(parse_action(a));
                if (it.is_object() && it.contains("link")) item.link = parse_nav(it["link"]);
                block.items.push_back(item);
            }
        }
        v.blocks.push_back(std::move(block));
    }
    return v;
}

Result parse_result(const json& j) {
    Result r;
    if (!j.is_object()) return r;
    r.message = str(j, "message");
    r.tone = str(j, "tone");
    if (j.contains("view") && j["view"].is_object()) r.view = parse_view(j["view"]);
    if (j.contains("navigate")) r.navigate = parse_nav(j["navigate"]);
    r.refresh = flag(j, "refresh");
    return r;
}

std::vector<Field> parse_fields(const json& j) {
    std::vector<Field> out;
    for (auto& f : arr(j, "fields")) out.push_back(parse_field(f));
    return out;
}

std::string base64_encode(const std::string& data) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        unsigned n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) |
                     static_cast<unsigned char>(data[i + 2]);
        out += t[n >> 18];
        out += t[(n >> 12) & 63];
        out += t[(n >> 6) & 63];
        out += t[n & 63];
    }
    if (i < data.size()) {
        unsigned n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i + 1]) << 8;
        out += t[n >> 18];
        out += t[(n >> 12) & 63];
        out += i + 1 < data.size() ? t[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

}
