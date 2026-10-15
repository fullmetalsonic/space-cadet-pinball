#include "update_check.h"
#include "release_version.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <vector>

#if defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace update_check
{
namespace
{
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool Numeric(const std::string& text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), Digit);
}
struct Version
{
    std::vector<std::string> Core, Pre;
};
bool Identifiers(const std::string& text, bool prerelease, std::vector<std::string>& parts)
{
    size_t start = 0;
    while (start < text.size())
    {
        const auto end = text.find('.', start);
        auto part = text.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](char c) { return Alpha(c) || Digit(c) || c == '-'; })) return false;
        if (prerelease && Numeric(part) && part.size() > 1 && part[0] == '0') return false;
        parts.push_back(part);
        if (end == std::string::npos) return true;
        start = end + 1;
    }
    return false;
}
bool ParseVersion(std::string text, Version& version)
{
    if (text.empty() || text.size() > 128) return false;
    if (text[0] == 'v') text.erase(0, 1);
    const auto build = text.find('+');
    if (build != std::string::npos)
    {
        std::vector<std::string> ignored;
        if (!Identifiers(text.substr(build + 1), false, ignored)) return false;
        text.erase(build);
    }
    const auto pre = text.find('-');
    if (pre != std::string::npos)
    {
        if (!Identifiers(text.substr(pre + 1), true, version.Pre)) return false;
        text.erase(pre);
    }
    if (!Identifiers(text, false, version.Core) || version.Core.size() != 3) return false;
    for (const auto& component : version.Core)
        if (!Numeric(component) || (component.size() > 1 && component[0] == '0')) return false;
    return true;
}
int NumericOrder(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a == b ? 0 : (a < b ? -1 : 1);
}

// A bounded structural JSON reader. Only the three required top-level fields
// are retained; nested values are validated and skipped, never searched for tags.
class ReleaseJson
{
public:
    explicit ReleaseJson(const std::string& text) : Text(text) {}
    bool Parse()
    {
        if (Text.empty() || Text.size() > MaxResponseBytes || !Object(0, true)) return false;
        Space();
        return At == Text.size() && HasTag && HasDraft && HasPrerelease;
    }
    std::string Tag;
    bool Draft = false, Prerelease = false;
private:
    const std::string& Text;
    size_t At = 0;
    bool HasTag = false, HasDraft = false, HasPrerelease = false;
    void Space() { while (At < Text.size() && (Text[At] == ' ' || Text[At] == '\n' || Text[At] == '\r' || Text[At] == '\t')) ++At; }
    bool Take(char c) { Space(); if (At == Text.size() || Text[At] != c) return false; ++At; return true; }
    bool Literal(const char* word)
    {
        Space();
        const std::string token(word);
        if (Text.compare(At, token.size(), token) != 0) return false;
        At += token.size(); return true;
    }
    bool Hex(unsigned& n)
    {
        if (Text.size() - At < 4) return false;
        n = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = Text[At++];
            const int digit = Digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            n = n * 16 + digit;
        }
        return true;
    }
    static void Utf8(std::string& out, unsigned n)
    {
        if (n < 0x80) out += static_cast<char>(n);
        else if (n < 0x800) { out += static_cast<char>(0xc0 | (n >> 6)); out += static_cast<char>(0x80 | (n & 63)); }
        else if (n < 0x10000) { out += static_cast<char>(0xe0 | (n >> 12)); out += static_cast<char>(0x80 | ((n >> 6) & 63)); out += static_cast<char>(0x80 | (n & 63)); }
        else { out += static_cast<char>(0xf0 | (n >> 18)); out += static_cast<char>(0x80 | ((n >> 12) & 63)); out += static_cast<char>(0x80 | ((n >> 6) & 63)); out += static_cast<char>(0x80 | (n & 63)); }
    }
    bool String(std::string& out)
    {
        if (!Take('"')) return false;
        while (At < Text.size())
        {
            const unsigned char c = static_cast<unsigned char>(Text[At++]);
            if (c == '"') return true;
            if (c < 32) return false;
            if (c != '\\')
            {
                if (c < 128) { out += static_cast<char>(c); continue; }
                unsigned cp = 0, count = 0;
                if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; count = 1; }
                else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; count = 2; }
                else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; count = 3; }
                else return false;
                if (Text.size() - At < count) return false;
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto next = static_cast<unsigned char>(Text[At++]);
                    if ((next & 0xc0) != 0x80) return false;
                    cp = (cp << 6) | (next & 63);
                }
                if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
                    cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
                Utf8(out, cp); continue;
            }
            if (At == Text.size()) return false;
            const char escape = Text[At++];
            switch (escape)
            {
            case '"': case '\\': case '/': out += escape; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned cp;
                if (!Hex(cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff)
                {
                    if (Text.size() - At < 2 || Text[At++] != '\\' || Text[At++] != 'u') return false;
                    unsigned low;
                    if (!Hex(low) || low < 0xdc00 || low > 0xdfff) return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                }
                else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                Utf8(out, cp); break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Boolean(bool& value)
    {
        Space();
        if (At < Text.size() && Text[At] == 't') { value = true; return Literal("true"); }
        value = false; return Literal("false");
    }
    bool Number()
    {
        Space();
        if (At < Text.size() && Text[At] == '-') ++At;
        if (At == Text.size()) return false;
        if (Text[At] == '0') ++At;
        else { if (Text[At] < '1' || Text[At] > '9') return false; while (At < Text.size() && Digit(Text[At])) ++At; }
        if (At < Text.size() && Text[At] == '.')
        { ++At; const auto start = At; while (At < Text.size() && Digit(Text[At])) ++At; if (At == start) return false; }
        if (At < Text.size() && (Text[At] == 'e' || Text[At] == 'E'))
        { ++At; if (At < Text.size() && (Text[At] == '+' || Text[At] == '-')) ++At; const auto start = At; while (At < Text.size() && Digit(Text[At])) ++At; if (At == start) return false; }
        return true;
    }
    bool Value(unsigned depth)
    {
        Space(); if (depth > 16 || At == Text.size()) return false;
        const char c = Text[At];
        if (c == '{') return Object(depth, false);
        if (c == '[')
        {
            ++At; Space(); if (At < Text.size() && Text[At] == ']') { ++At; return true; }
            do { if (!Value(depth + 1)) return false; Space(); if (At < Text.size() && Text[At] == ']') { ++At; return true; } } while (Take(','));
            return false;
        }
        if (c == '"') { std::string ignored; return String(ignored); }
        if (c == 't') return Literal("true");
        if (c == 'f') return Literal("false");
        if (c == 'n') return Literal("null");
        return Number();
    }
    bool Object(unsigned depth, bool root)
    {
        if (depth > 16 || !Take('{')) return false;
        std::set<std::string> keys;
        Space(); if (At < Text.size() && Text[At] == '}') { ++At; return true; }
        do
        {
            std::string key;
            if (!String(key) || !keys.insert(key).second || !Take(':')) return false;
            if (root && key == "tag_name") { HasTag = true; if (!String(Tag)) return false; }
            else if (root && key == "draft") { HasDraft = true; if (!Boolean(Draft)) return false; }
            else if (root && key == "prerelease") { HasPrerelease = true; if (!Boolean(Prerelease)) return false; }
            else if (!Value(depth + 1)) return false;
            Space(); if (At < Text.size() && Text[At] == '}') { ++At; return true; }
        } while (Take(','));
        return false;
    }
};
Result Failed(const std::string& detail) { Result result; result.State = Status::Failed; result.Detail = detail; return result; }
std::string TagUrl(const std::string& tag)
{
    // SemVer was already validated. Encode '+' instead of accepting a server URL.
    std::string encoded;
    for (char c : tag) encoded += c == '+' ? "%2B" : std::string(1, c);
    return std::string(ReleasesUrl) + "/tag/" + encoded;
}
}

bool CompareVersions(const std::string& left, const std::string& right, int& order)
{
    Version a, b;
    if (!ParseVersion(left, a) || !ParseVersion(right, b)) return false;
    for (size_t i = 0; i < 3; ++i)
        if ((order = NumericOrder(a.Core[i], b.Core[i])) != 0) return true;
    if (a.Pre.empty() || b.Pre.empty()) { order = a.Pre.empty() == b.Pre.empty() ? 0 : a.Pre.empty() ? 1 : -1; return true; }
    for (size_t i = 0; i < std::min(a.Pre.size(), b.Pre.size()); ++i)
    {
        const bool an = Numeric(a.Pre[i]), bn = Numeric(b.Pre[i]);
        order = an && bn ? NumericOrder(a.Pre[i], b.Pre[i]) : an != bn ? (an ? -1 : 1) : a.Pre[i] == b.Pre[i] ? 0 : a.Pre[i] < b.Pre[i] ? -1 : 1;
        if (order) return true;
    }
    order = a.Pre.size() == b.Pre.size() ? 0 : a.Pre.size() < b.Pre.size() ? -1 : 1;
    return true;
}

Result EvaluateResponse(const std::string& current, unsigned httpStatus, const std::string& body, bool transportOk)
{
    if (!transportOk) return Failed("Network request failed or timed out.");
    if (httpStatus == 404) { Result result; result.State = Status::NoStableRelease; return result; }
    if (httpStatus != 200) return Failed("GitHub HTTP " + std::to_string(httpStatus));
    ReleaseJson release(body);
    if (!release.Parse()) return Failed("Invalid GitHub release response.");
    Version stable;
    if (!ParseVersion(release.Tag, stable)) return Failed("Invalid release version.");
    if (release.Draft || release.Prerelease || !stable.Pre.empty()) return Failed("Latest response is not a public stable release.");
    int order = 0;
    if (!CompareVersions(current, release.Tag, order)) return Failed("Invalid current version.");
    Result result;
    result.State = order < 0 ? Status::Available : Status::Current;
    result.LatestVersion = release.Tag;
    result.ReleaseUrl = TagUrl(release.Tag);
    return result;
}

const char* StatusText(Status status, bool korean)
{
    switch (status)
    {
    case Status::Idle: return korean ? u8"아직 확인하지 않았습니다." : "Not checked yet.";
    case Status::Checking: return korean ? u8"확인 중..." : "Checking...";
    case Status::Current: return korean ? u8"업데이트가 필요하지 않습니다." : "No update needed.";
    case Status::Available: return korean ? u8"새 정식 버전이 있습니다." : "A stable update is available.";
    case Status::NoStableRelease: return korean ? u8"게시된 정식 버전이 없습니다." : "No stable release is published.";
    default: return korean ? u8"확인하지 못했습니다. 나중에 다시 시도하세요." : "Could not check. Try again later.";
    }
}

#if defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace
{
using Clock = std::chrono::steady_clock;
// The handle retains one reference until its final HANDLE_CLOSING notification.
// This allows shutdown to join only our bounded worker, without waiting for OS
// callbacks or leaving a detached thread or a callback pointing at the manager.
struct AsyncRequest
{
    std::atomic<unsigned> References{2};
    std::mutex Mutex;
    std::condition_variable Event;
    DWORD Notification = 0, Count = 0;
    bool Error = false;
    char Buffer[4096]{};
    void Release() { if (References.fetch_sub(1) == 1) delete this; }
};
void CALLBACK Notify(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length)
{
    auto request = reinterpret_cast<AsyncRequest*>(context);
    if (!request) return;
    request->References.fetch_add(1); // Retain through this callback's return.
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { request->Release(); request->Release(); return; }
    {
        std::lock_guard<std::mutex> lock(request->Mutex);
        request->Notification = status;
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) request->Error = true;
        if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE && length == sizeof(DWORD)) request->Count = *static_cast<DWORD*>(info);
        if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) request->Count = length;
    }
    request->Event.notify_one();
    request->Release();
}
class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : Handle(handle) {}
    ~InternetHandle() { if (Handle) WinHttpCloseHandle(Handle); }
    operator HINTERNET() const { return Handle; }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
private:
    HINTERNET Handle;
};
bool Await(AsyncRequest& request, DWORD notification, const std::atomic<bool>& stopping, Clock::time_point deadline)
{
    const auto stageEnd = std::min(deadline, Clock::now() + std::chrono::milliseconds(1500));
    std::unique_lock<std::mutex> lock(request.Mutex);
    while (!stopping.load() && !request.Error && Clock::now() < stageEnd)
    {
        if (request.Notification == notification) return true;
        request.Event.wait_for(lock, std::chrono::milliseconds(25));
    }
    return false;
}
void Prepare(AsyncRequest& request)
{
    std::lock_guard<std::mutex> lock(request.Mutex);
    request.Notification = 0; request.Count = 0;
}
Result Fetch(const std::atomic<bool>& stopping)
{
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    InternetHandle session(WinHttpOpen(L"SpaceCadetPinball/" PINBALL_RELEASE_VERSION,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session) return Failed("Could not initialize WinHTTP.");
    WinHttpSetTimeouts(session, 1500, 1500, 1500, 1500);
    InternetHandle connection(WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection || stopping.load()) return Failed("Network request cancelled.");
    auto request = new AsyncRequest;
    HINTERNET handle = WinHttpOpenRequest(connection, L"GET", L"/repos/fullmetalsonic/space-cadet-pinball/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!handle) { request->Release(); request->Release(); return Failed("Could not create update request."); }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(request);
    if (!WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
        WinHttpSetStatusCallback(handle, Notify, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        WinHttpCloseHandle(handle); request->Release(); request->Release(); return Failed("Could not configure update request.");
    }
    // Close before releasing the worker reference; callbacks may run during close.
    struct RequestScope
    {
        HINTERNET Handle; AsyncRequest* Request;
        ~RequestScope() { WinHttpCloseHandle(Handle); Request->Release(); }
    } scope{handle, request};
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) return Failed("Could not restrict update request redirects.");
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (stopping.load() || !WinHttpSendRequest(handle, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, context) ||
        !Await(*request, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, stopping, deadline)) return EvaluateResponse(PINBALL_RELEASE_VERSION, 0, "", false);
    Prepare(*request);
    if (!WinHttpReceiveResponse(handle, nullptr) || !Await(*request, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, stopping, deadline)) return EvaluateResponse(PINBALL_RELEASE_VERSION, 0, "", false);
    DWORD code = 0, codeSize = sizeof(code);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX)) return Failed("Could not read GitHub status.");
    if (code != 200) return EvaluateResponse(PINBALL_RELEASE_VERSION, code, "");
    std::string body;
    while (!stopping.load() && Clock::now() < deadline)
    {
        Prepare(*request);
        if (!WinHttpQueryDataAvailable(handle, nullptr) || !Await(*request, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, stopping, deadline)) return EvaluateResponse(PINBALL_RELEASE_VERSION, 0, "", false);
        DWORD available;
        { std::lock_guard<std::mutex> lock(request->Mutex); available = request->Count; }
        if (!available) return EvaluateResponse(PINBALL_RELEASE_VERSION, code, body);
        if (available > MaxResponseBytes - body.size()) return Failed("Release response exceeded size limit.");
        Prepare(*request);
        if (!WinHttpReadData(handle, request->Buffer, std::min<DWORD>(available, sizeof(request->Buffer)), nullptr) ||
            !Await(*request, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, stopping, deadline)) return EvaluateResponse(PINBALL_RELEASE_VERSION, 0, "", false);
        DWORD count;
        { std::lock_guard<std::mutex> lock(request->Mutex); count = request->Count; }
        if (!count) return EvaluateResponse(PINBALL_RELEASE_VERSION, code, body);
        if (count > MaxResponseBytes - body.size()) return Failed("Release response exceeded size limit.");
        body.append(request->Buffer, count);
    }
    return EvaluateResponse(PINBALL_RELEASE_VERSION, 0, "", false);
}
}
Manager::~Manager() { Shutdown(); }
bool Manager::Request()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (Stopping.load() || Latest.State == Status::Checking) return false;
    Latest = Result{}; Latest.State = Status::Checking; Pending = true;
    if (!Worker.joinable())
    {
        try { Worker = std::thread(&Manager::Run, this); }
        catch (...) { Pending = false; Latest = Failed("Could not start update worker."); return false; }
    }
    Wake.notify_one(); return true;
}
Result Manager::Snapshot() const { std::lock_guard<std::mutex> lock(Mutex); return Latest; }
void Manager::Shutdown()
{
    Stopping.store(true); Wake.notify_all();
    if (Worker.joinable()) Worker.join();
}
void Manager::Run()
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(Mutex);
            Wake.wait(lock, [this] { return Pending || Stopping.load(); });
            if (Stopping.load()) return;
            Pending = false;
        }
        Result result;
        try { result = Fetch(Stopping); }
        catch (...) { result = Failed("Update request could not complete."); }
        std::lock_guard<std::mutex> lock(Mutex);
        Latest = result;
        if (Stopping.load()) return;
    }
}
#endif
}
