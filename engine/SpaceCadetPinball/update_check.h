#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace update_check
{
constexpr const char* ReleasesUrl = "https://github.com/fullmetalsonic/space-cadet-pinball/releases";
constexpr size_t MaxResponseBytes = 256 * 1024;

enum class Status { Idle, Checking, Current, Available, NoStableRelease, Failed };
struct Result
{
    Status State = Status::Idle;
    std::string LatestVersion;
    std::string ReleaseUrl = ReleasesUrl;
    std::string Detail;
};

// Strict SemVer comparison, with an optional leading v on release tags.
bool CompareVersions(const std::string& left, const std::string& right, int& order);
Result EvaluateResponse(const std::string& current, unsigned httpStatus,
    const std::string& body, bool transportOk = true);
const char* StatusText(Status status, bool korean);

#if defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
// Owns one worker for the entire application session, including game restarts.
// Its transport callbacks own no game, SDL, ImGui or Manager references.
class Manager
{
public:
    Manager() = default;
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    bool Request(); // Requests while checking are coalesced.
    Result Snapshot() const;
    void Shutdown();
private:
    void Run();
    mutable std::mutex Mutex;
    std::condition_variable Wake;
    std::thread Worker;
    std::atomic<bool> Stopping{false};
    bool Pending = false;
    Result Latest;
};
#endif
}
