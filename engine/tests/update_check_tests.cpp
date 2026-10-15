#include "update_check.h"
#include "release_version.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
}
std::string Release(const std::string& tag)
{
    return "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false}";
}
void Compare(const char* a, const char* b, int expected)
{
    int order = 42;
    Check(update_check::CompareVersions(a, b, order) && order == expected, (std::string(a) + " vs " + b).c_str());
}
}
int main(int argc, char** argv)
{
#if defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Explicit test-tool modes call the exact product Manager/transport. No
    // endpoint, fixture, environment override or injected response exists in it.
    if (argc > 1 && (std::string(argv[1]) == "--live" || std::string(argv[1]) == "--cancel"))
    {
        update_check::Manager manager;
        const auto began = std::chrono::steady_clock::now();
        Check(manager.Request(), "native request starts");
        Check(!manager.Request(), "in-flight manual request coalesces with startup request");
        const bool cancel = std::string(argv[1]) == "--cancel";
        if (!cancel)
        {
            for (unsigned i = 0; i < 400 && manager.Snapshot().State == update_check::Status::Checking; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const auto result = manager.Snapshot();
            std::printf("live state=%d current=%s latest=%s url=%s detail=%s\n", static_cast<int>(result.State), PINBALL_RELEASE_VERSION,
                result.LatestVersion.c_str(), result.ReleaseUrl.c_str(), result.Detail.c_str());
            Check(result.State == update_check::Status::Current || result.State == update_check::Status::Available ||
                result.State == update_check::Status::NoStableRelease, "real API returns verified policy result");
            Check(manager.Request(), "manual request can start after completion");
            for (unsigned i = 0; i < 400 && manager.Snapshot().State == update_check::Status::Checking; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const auto manual = manager.Snapshot();
            std::printf("manual state=%d latest=%s detail=%s\n", static_cast<int>(manual.State), manual.LatestVersion.c_str(), manual.Detail.c_str());
            Check(manual.State == update_check::Status::Current || manual.State == update_check::Status::Available ||
                manual.State == update_check::Status::NoStableRelease, "manual real API returns verified policy result");
        }
        else std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto shutdown = std::chrono::steady_clock::now();
        manager.Shutdown();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shutdown).count();
        Check(elapsed < 2000, "cancel and join is bounded below 2 seconds");
        Check(!manager.Request(), "request after final shutdown is rejected");
        std::printf("native checks=%d failures=%d shutdown_ms=%lld total_ms=%lld\n", checks, failures,
            static_cast<long long>(elapsed), static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count()));
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
#endif
    (void)argc; (void)argv;
    using update_check::EvaluateResponse;
    using update_check::Status;
    Compare("1.2.3", "v1.2.3", 0);
    Compare("1.2.3+one", "1.2.3+two", 0);
    Compare("1.2.9", "1.2.10", -1);
    Compare("2.0.0", "1.99.99", 1);
    Compare("1.0.0-alpha", "1.0.0-alpha.1", -1);
    Compare("1.0.0-alpha.9", "1.0.0-alpha.10", -1);
    Compare("1.0.0-alpha.10", "1.0.0-beta", -1);
    Compare("1.0.0-1", "1.0.0-alpha", -1);
    Compare("1.0.0-rc.1", "1.0.0", -1);
    Compare("0.1.1-preview.1", "0.1.1", -1);
    Compare("99999999999999999999.0.0", "99999999999999999998.0.0", 1);
    const std::vector<std::string> badVersions = {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3+", "1.2.3-a..b", "1.2.3-01", "1.2.3+a..b", "1.2.3+x+y", "V1.2.3", " 1.2.3", "1.2.3/evil", "vhttps://evil.test"};
    for (const auto& version : badVersions) { int order; Check(!update_check::CompareVersions(version, "1.2.3", order), ("reject " + version).c_str()); }
    Check(EvaluateResponse("1.2.3", 200, Release("v1.2.3")).State == Status::Current, "same version");
    Check(EvaluateResponse("1.2.3", 200, Release("1.2.4")).State == Status::Available, "new stable");
    Check(EvaluateResponse("1.2.3", 200, Release("1.2.2")).State == Status::Current, "older stable does not downgrade");
    Check(EvaluateResponse(PINBALL_RELEASE_VERSION, 200, Release("v0.1.1")).State == Status::Available, "current preview upgrades to same-core stable");
    Check(EvaluateResponse("1.2.3", 200, Release("1.2.3+build")).State == Status::Current, "build metadata ignored");
    const auto safe = EvaluateResponse("1.0.0", 200, "{\"tag_name\":\"v2.0.0+build\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://evil.test\"}");
    Check(safe.ReleaseUrl == std::string(update_check::ReleasesUrl) + "/tag/v2.0.0%2Bbuild", "fixed repository URL with encoded validated tag");
    Check(EvaluateResponse("1.0.0", 200, "{\"tag_name\":\"v2.0.0\",\"draft\":false,\"prerelease\":false,\"body\":{\"tag_name\":\"v9.0.0\",\"items\":[null,true,false,-0.1e+2,{\"x\":\"escaped \\\" \\uD83D\\uDE00\"}]}}").LatestVersion == "v2.0.0", "nested fields ignored structurally, Unicode escapes accepted");
    Check(EvaluateResponse("1.0.0", 200, "{\"tag\\u005fname\":\"v2.0.0\",\"draft\":false,\"prerelease\":false}").State == Status::Available, "escaped root field name");
    const std::vector<std::string> badJson = {
        "", "[]", "{}", "null", "{\"tag_name\":\"2.0.0\",\"draft\":false}",
        "{\"tag_name\":2,\"draft\":false,\"prerelease\":false}",
        "{\"tag_name\":\"2.0.0\",\"draft\":\"false\",\"prerelease\":false}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":null}",
        "{\"tag_name\":\"2.0.0\",\"tag_name\":\"3.0.0\",\"draft\":false,\"prerelease\":false}",
        "{\"tag_name\":\"2.0.0\",\"tag\\u005fname\":\"3.0.0\",\"draft\":false,\"prerelease\":false}",
        Release("2.0.0") + " trailing", "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,}",
        "{\"nested\":{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false}}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"x\":01}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"x\":1.}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"x\":\"\\uD800\"}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"x\":\"\\uDC00\"}",
        "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"x\":\"\\q\"}"
    };
    for (const auto& body : badJson) Check(EvaluateResponse("1.0.0", 200, body).State == Status::Failed, "reject malformed/typed/duplicate/nested response");
    const auto complete = Release("2.0.0");
    for (size_t n = 0; n < complete.size(); ++n) Check(EvaluateResponse("1.0.0", 200, complete.substr(0, n)).State == Status::Failed, "every truncated prefix rejected");
    for (const auto& tag : {"v2.0.0-rc.1", "2.0", "2.0.0/evil", "https://evil.test"})
        Check(EvaluateResponse("1.0.0", 200, Release(tag)).State == Status::Failed, "non-stable or invalid tag rejected");
    Check(EvaluateResponse("1.0.0", 200, "{\"tag_name\":\"2.0.0\",\"draft\":true,\"prerelease\":false}").State == Status::Failed, "draft rejected");
    Check(EvaluateResponse("1.0.0", 200, "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":true}").State == Status::Failed, "prerelease flag rejected");
    Check(EvaluateResponse("bad current", 200, Release("2.0.0")).State == Status::Failed, "invalid current cannot claim current");
    std::string deep = "0"; for (int i = 0; i < 20; ++i) deep = "[" + deep + "]";
    Check(EvaluateResponse("1.0.0", 200, "{\"tag_name\":\"2.0.0\",\"draft\":false,\"prerelease\":false,\"deep\":" + deep + "}").State == Status::Failed, "depth limit");
    Check(EvaluateResponse("1.0.0", 200, complete + std::string(update_check::MaxResponseBytes, ' ')).State == Status::Failed, "size limit");
    for (const auto code : {403u, 429u, 500u, 301u, 0u})
    {
        const auto response = EvaluateResponse("1.0.0", code, complete);
        Check(response.State == Status::Failed && response.LatestVersion.empty(), "non-200 never claims latest");
    }
    Check(EvaluateResponse("1.0.0", 404, "{}").State == Status::NoStableRelease, "404 means no published stable release");
    Check(EvaluateResponse("1.0.0", 200, complete, false).State == Status::Failed, "network failure overrides body");
    Check(EvaluateResponse("1.0.0", 404, "", false).State == Status::Failed, "network failure does not become no-release");
    std::printf("update policy checks=%d failures=%d\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
