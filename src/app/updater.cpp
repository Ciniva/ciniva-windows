#include "app/updater.h"

#include "app/dispatch.h"
#include "app/timing.h"
#include "core/http.h"
#include "core/settings.h"
#include "core/text.h"
#include "version.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace ciniva {

namespace {

constexpr const wchar_t* kFiles[] = {L"ciniva.exe", L"libmpv-2.dll"};

std::wstring exe_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring dir_of(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }

bool file_exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

HttpResponse fetch(const std::string& url, int timeout_ms) {
    HttpRequest req;
    req.url = url;
    req.timeout_ms = timeout_ms;
    req.headers.push_back({"User-Agent", "Ciniva-Windows/" CINIVA_VERSION});
    req.headers.push_back({"Accept", "application/vnd.github+json"});
    return http_send(req);
}

bool run_wait(const std::wstring& cmdline, DWORD* exit_code) {
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = cmdline;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void remove_tree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring p = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// Downloads, verifies and swaps in a release; returns an error message or empty on success.
std::string apply(const update::Release& r) {
    auto zip = fetch(r.archive_url, 300000);
    if (!zip.ok()) return zip.status ? "Download failed (HTTP " + std::to_string(zip.status) + ")." : zip.error;
    auto sig = fetch(r.signature_url, 30000);
    if (!sig.ok()) return "Couldn't download the update's signature.";
    if (!update::verify(zip.body, trim(sig.body))) return "The update's signature is invalid; it was not installed.";

    std::wstring dir = dir_of(exe_path());
    std::wstring work = dir + L"\\.update";
    remove_tree(work);
    if (!CreateDirectoryW(work.c_str(), nullptr)) return "Can't write to " + narrow(dir) + ".";
    std::wstring zip_path = work + L"\\update.zip";
    {
        std::ofstream out(zip_path, std::ios::binary);
        out.write(zip.body.data(), static_cast<std::streamsize>(zip.body.size()));
        if (!out) return "Couldn't save the update.";
    }
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    DWORD code = 1;
    if (!run_wait(L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + zip_path + L"\" -C \"" + work + L"\"", &code) || code != 0)
        return "Couldn't unpack the update.";
    for (auto* f : kFiles)
        if (!file_exists(work + L"\\" + f)) return "The update is missing " + narrow(f) + ".";

    // Running images can be renamed but not overwritten, so the current files step aside as .old.
    for (auto* f : kFiles) {
        std::wstring cur = dir + L"\\" + f, old = cur + L".old";
        DeleteFileW(old.c_str());
        if (file_exists(cur) && !MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            for (auto* g : kFiles) MoveFileExW((dir + L"\\" + g + L".old").c_str(), (dir + L"\\" + g).c_str(), 0);
            return "Couldn't replace " + narrow(f) + ".";
        }
    }
    for (auto* f : kFiles) {
        if (!MoveFileExW((work + L"\\" + f).c_str(), (dir + L"\\" + f).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            for (auto* g : kFiles) {
                DeleteFileW((dir + L"\\" + g).c_str());
                MoveFileExW((dir + L"\\" + g + L".old").c_str(), (dir + L"\\" + g).c_str(), 0);
            }
            return "Couldn't install " + narrow(f) + ".";
        }
    }
    remove_tree(work);
    return {};
}

}

void Updater::start_automatic_checks() {
    std::weak_ptr<bool> alive = alive_;
    dispatch::background([this, alive] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        dispatch::post([this, alive] {
            if (alive.expired()) return;
            if (Settings::get().auto_update) check(false);
            schedule();
        });
    });
}

void Updater::schedule() {
    std::weak_ptr<bool> alive = alive_;
    dispatch::background([this, alive] {
        std::this_thread::sleep_for(std::chrono::hours(6));
        dispatch::post([this, alive] {
            if (alive.expired()) return;
            if (Settings::get().auto_update) check(false);
            schedule();
        });
    });
}

void Updater::check(bool user_initiated) {
    if (phase_ == Phase::Installing || phase_ == Phase::Checking) return;
    phase_ = Phase::Checking;
    changed_();
    std::weak_ptr<bool> alive = alive_;
    struct Outcome {
        std::optional<update::Release> release;
        std::string error;
    };
    dispatch::async(
        [] {
            auto r = fetch(std::string("https://api.github.com/repos/") + update::kRepository + "/releases/latest", 20000);
            if (r.status == 404) return Outcome{};
            if (!r.ok()) return Outcome{std::nullopt, r.status ? "GitHub returned HTTP " + std::to_string(r.status) + "." : r.error};
            return Outcome{update::parse_release(r.body), ""};
        },
        [this, alive, user_initiated](Outcome o) {
            if (alive.expired()) return;
            if (!o.error.empty()) {
                timing::log("update check failed: %s", o.error.c_str());
                phase_ = user_initiated ? Phase::Failed : Phase::Idle;
                error_ = "Couldn't check for updates: " + o.error;
            } else if (o.release && update::is_newer(o.release->version, CINIVA_VERSION)) {
                available_ = o.release;
                phase_ = Phase::Available;
                timing::log("update available: %s", o.release->version.c_str());
            } else {
                phase_ = Phase::UpToDate;
            }
            changed_();
        });
}

void Updater::install() {
    if (!available_ || phase_ == Phase::Installing) return;
    phase_ = Phase::Installing;
    changed_();
    update::Release r = *available_;
    std::weak_ptr<bool> alive = alive_;
    dispatch::async([r] { return apply(r); }, [this, alive](std::string error) {
        if (alive.expired()) return;
        if (!error.empty()) {
            phase_ = Phase::Failed;
            error_ = error;
            changed_();
            return;
        }
        std::wstring exe = exe_path();
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"\"" + exe + L"\"";
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        quit_();
    });
}

std::string Updater::status() const {
    switch (phase_) {
    case Phase::Checking: return "Checking for updates\xE2\x80\xA6";
    case Phase::UpToDate: return "Ciniva is up to date.";
    case Phase::Available: return "Version " + available_->version + " is available.";
    case Phase::Installing: return "Installing the update\xE2\x80\xA6";
    case Phase::Failed: return error_;
    default: return "";
    }
}

void Updater::remove_leftovers() {
    std::wstring dir = dir_of(exe_path());
    for (auto* f : kFiles) DeleteFileW((dir + L"\\" + f + L".old").c_str());
    remove_tree(dir + L"\\.update");
}

bool Updater::install_per_user(std::wstring* error) {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_UserProgramFiles, KF_FLAG_CREATE, nullptr, &base))) {
        *error = L"Can't find the per-user Programs folder.";
        return false;
    }
    std::wstring target = std::wstring(base) + L"\\Ciniva";
    CoTaskMemFree(base);
    SHCreateDirectoryExW(nullptr, target.c_str(), nullptr);
    std::wstring src = dir_of(exe_path());
    if (_wcsicmp(src.c_str(), target.c_str()) != 0) {
        for (auto* f : kFiles) {
            std::wstring to = target + L"\\" + f;
            if (!CopyFileW((src + L"\\" + f).c_str(), to.c_str(), FALSE)) {
                std::wstring old = to + L".old";
                DeleteFileW(old.c_str());
                MoveFileExW(to.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
                if (!CopyFileW((src + L"\\" + f).c_str(), to.c_str(), FALSE)) {
                    *error = L"Couldn't copy " + std::wstring(f) + L" to " + target + L".";
                    return false;
                }
            }
        }
    }

    PWSTR programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) {
        std::wstring lnk = std::wstring(programs) + L"\\Ciniva.lnk";
        CoTaskMemFree(programs);
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
            std::wstring exe = target + L"\\ciniva.exe";
            link->SetPath(exe.c_str());
            link->SetWorkingDirectory(target.c_str());
            link->SetDescription(L"Ciniva");
            Microsoft::WRL::ComPtr<IPersistFile> file;
            if (SUCCEEDED(link.As(&file))) file->Save(lnk.c_str(), TRUE);
        }
    }

    std::wstring exe = target + L"\\ciniva.exe";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, target.c_str(), &si, &pi)) {
        *error = L"Installed to " + target + L" but couldn't start it.";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}
