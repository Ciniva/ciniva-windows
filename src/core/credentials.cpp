#include "core/credentials.h"

#include "core/text.h"

#include <windows.h>
#include <wincred.h>

namespace ciniva::credentials {

static std::wstring target(const std::string& server) { return L"Ciniva/" + widen(server); }

std::optional<std::string> read(const std::string& server) {
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target(server).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return std::nullopt;
    std::string token(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred);
    return token;
}

bool save(const std::string& server, const std::string& username, const std::string& token) {
    std::wstring t = target(server);
    std::wstring user = widen(username);
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = t.data();
    cred.UserName = user.data();
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
    cred.CredentialBlobSize = static_cast<DWORD>(token.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&cred, 0) != FALSE;
}

void remove(const std::string& server) { CredDeleteW(target(server).c_str(), CRED_TYPE_GENERIC, 0); }

}
