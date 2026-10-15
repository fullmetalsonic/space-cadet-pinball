#include "pch.h"
#include "hq_render.h"
#include "gdrv.h"
#include "zdrv.h"
#include "winmain.h"
#include "pb.h"
#include "timer.h"
#include "nudge.h"
#include "options.h"
#include "pinball_platform.h"
#include "fullscrn.h"
#include "font_selection.h"
#include "Sound.h"
#include "render.h"
#include "GroupData.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TTextBox.h"
#include "score.h"
#include "translations.h"
#include "TBall.h"
#include "TFlipper.h"
#include "TFlipperEdge.h"
#include <iomanip>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include <fstream>
#include <stdexcept>

namespace {
int failures = 0;
void check(bool ok, const std::string& message)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", message.c_str());
    if (!ok) ++failures;
}
std::vector<uint8_t> bytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Missing fixture: " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
std::vector<uint32_t> words32(const std::string& path)
{
    const auto b = bytes(path);
    if (b.size() % 4) throw std::runtime_error("Invalid golden length");
    std::vector<uint32_t> result;
    for (size_t i = 0; i < b.size(); i += 4)
        result.push_back(uint32_t(b[i]) | (uint32_t(b[i + 1]) << 8) | (uint32_t(b[i + 2]) << 16) | (uint32_t(b[i + 3]) << 24));
    return result;
}
std::vector<uint16_t> words16(const std::string& path)
{
    const auto b = bytes(path);
    if (b.size() % 2) throw std::runtime_error("Invalid depth length");
    std::vector<uint16_t> result;
    for (size_t i = 0; i < b.size(); i += 2) result.push_back(uint16_t(b[i] | (uint16_t(b[i + 1]) << 8)));
    return result;
}
std::vector<uint32_t> colors(const std::vector<ColorRgba>* pixels)
{
    std::vector<uint32_t> result;
    if (pixels) for (const auto pixel : *pixels) result.push_back(pixel.Color);
    return result;
}
std::vector<uint32_t> raw(const gdrv_bitmap8& b)
{
    std::vector<uint32_t> result;
    for (int y = 0; y < b.Height; ++y) for (int x = 0; x < b.Width; ++x)
        result.push_back(b.BmpBufPtr1[y * b.Stride + x].Color);
    return result;
}
template<class T> std::vector<T> expanded(const std::vector<T>& input, int width, int height, int scale)
{
    std::vector<T> output;
    for (int y = 0; y < height * scale; ++y) for (int x = 0; x < width * scale; ++x)
        output.push_back(input[(y / scale) * width + x / scale]);
    return output;
}
void put(gdrv_bitmap8& bitmap, const std::vector<uint32_t>& input)
{
    if (input.size() != size_t(bitmap.Width * bitmap.Height)) throw std::runtime_error("Invalid bitmap fixture");
    for (int y = 0; y < bitmap.Height; ++y) for (int x = 0; x < bitmap.Width; ++x)
        bitmap.BmpBufPtr1[y * bitmap.Stride + x] = ColorRgba(input[y * bitmap.Width + x]);
}
struct Fixture
{
    std::string Root;
    gdrv_bitmap8 Screen{4, 3, false}, Sprite{4, 2, false}, Ball{3, 2, false}, Glyph{2, 1, false}, Backup{4, 3, false};
    zmap_header_type Depth{4, 3, 4}, SpriteDepth{3, 2, 3};
    explicit Fixture(const std::string& root, const std::string& pack = "original-1x.pbhq") : Root(root)
    {
        hq_render::Shutdown();
        // Deliberately padded RGB storage: only three logical pixels per row
        // belong to the source identity; the poison padding must be excluded.
        Sprite.Width = 3;
        Sprite.BmpBufPtr1[3] = Sprite.BmpBufPtr1[7] = ColorRgba(0xDEADC0DE);
        put(Screen, std::vector<uint32_t>(12, 0x02000010));
        put(Sprite, words32(root + "/sprite.bgra")); put(Ball, words32(root + "/ball.bgra")); put(Glyph, words32(root + "/glyph.bgra"));
        Sprite.XPosition = -1; Sprite.YPosition = 2;
        auto z = words16(root + "/initial-depth.u16"); std::copy(z.begin(), z.end(), Depth.ZPtr1);
        z = words16(root + "/sprite-depth.u16"); std::copy(z.begin(), z.end(), SpriteDepth.ZPtr1);
        gdrv_bitmap8* backups[]{&Backup};
        std::vector<hq_render::Binding> bindings{{7, 0, &Sprite}, {8, 0, &Ball}, {9, 0, &Glyph}};
        hq_render::Initialize(&Screen, &Depth, backups, 1, bindings, root + "/source.dat", root + "/" + pack);
    }
    ~Fixture() { hq_render::Shutdown(); }
    void sprite() { zdrv::paint(3, 2, &Screen, 0, 0, &Depth, 0, 0, &Sprite, 0, 0, &SpriteDepth, 0, 0); }
    void ball() { zdrv::paint_flat(3, 2, &Screen, 0, 0, &Depth, 0, 0, &Ball, 0, 0, 3); }
};
void hashes()
{
    const char* multi = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    check(hq_render::Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 standard empty vector");
    check(hq_render::Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 standard abc vector");
    check(hq_render::Sha256Hex(multi, strlen(multi)) == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", "SHA256 standard 112-byte multi-block vector");
}
void composition(const std::string& root)
{
    {
        Fixture f(root);
        check(!hq_render::Enabled(), "OFF starts on original renderer");
        f.sprite(); f.ball();
        check(raw(f.Screen) == words32(root + "/expected-ball.bgra"), "OFF original composition matches independently specified golden");
        check(!hq_render::ScreenPixelsForTest(), "OFF has no active display sidecar");
    }
    for (int scale : {1, 2}) {
        Fixture f(root, scale == 1 ? "original-1x.pbhq" : "original-2x.pbhq");
        const auto sourceBefore = raw(f.Sprite);
        check(hq_render::Enable(true) && hq_render::Scale() == scale, "Strict complete original RGB pack loads at scale " + std::to_string(scale));
        f.sprite();
        check(colors(hq_render::ScreenPixelsForTest()) == expanded(words32(root + "/expected-sprite.bgra"), 4, 3, scale), "Sprite >= depth equality, literal Color0 and alpha2 golden at scale " + std::to_string(scale));
        const auto expectedDepth = expanded(words16(root + "/expected-depth.u16"), 4, 3, scale);
        check(hq_render::DepthPixelsForTest() && *hq_render::DepthPixelsForTest() == expectedDepth, "Sprite depth golden at scale " + std::to_string(scale));
        f.ball();
        check(colors(hq_render::ScreenPixelsForTest()) == expanded(words32(root + "/expected-ball.bgra"), 4, 3, scale), "Ball strict depth tie, transparent Color0 and nonzero alpha0 golden at scale " + std::to_string(scale));
        check(hq_render::DepthPixelsForTest() && *hq_render::DepthPixelsForTest() == expectedDepth, "Ball never writes depth at scale " + std::to_string(scale));
        check(raw(f.Screen) == words32(root + "/expected-ball.bgra") && raw(f.Sprite) == sourceBefore && f.Sprite.Width == 3 && f.Sprite.Height == 2 && f.Sprite.Stride == 4 && f.Sprite.XPosition == -1 && f.Sprite.YPosition == 2 && f.Sprite.BmpBufPtr1[3].Color == 0xDEADC0DE && f.Sprite.BmpBufPtr1[7].Color == 0xDEADC0DE, "HQ preserves original pixels/metadata and excludes poisoned padding from source hash");
    }
    {
        Fixture f(root, "detail-2x.pbhq");
        check(hq_render::Enable(true), "Detailed 2x RGB fixture loads"); f.sprite();
        auto detail = expanded(words32(root + "/expected-sprite.bgra"), 4, 3, 2);
        detail[0] = 0x02C00000; detail[1] = 0x0200C000; detail[8] = 0x020000C0; detail[9] = 0x02C0C000;
        check(colors(hq_render::ScreenPixelsForTest()) == detail, "Display sidecar keeps independently chosen subpixel RGB with original alpha");
        gdrv::copy_bitmap(&f.Backup, 4, 3, 0, 0, &f.Screen, 0, 0);
        f.ball();
        gdrv::copy_bitmap(&f.Screen, 4, 3, 0, 0, &f.Backup, 0, 0);
        check(colors(hq_render::ScreenPixelsForTest()) == detail, "Ball-style backup/restore retains HQ subpixel detail");
        auto original = raw(f.Screen);
        hq_render::Enable(false);
        check(!hq_render::Enabled() && raw(f.Screen) == original, "Turning HQ OFF leaves original screen unmodified");
    }
    {
        Fixture f(root); check(hq_render::Enable(true), "Clipped-region fixture loads");
        // The caller's clipped 3x2 rectangle at (-1,1) maps to source (1,0),
        // destination (0,1), size 2x2. Both source and destination offsets matter.
        gdrv::copy_bitmap(&f.Screen, 2, 2, 0, 1, &f.Sprite, 1, 0);
        check(colors(hq_render::ScreenPixelsForTest()) == words32(root + "/expected-copy-clip.bgra") && raw(f.Screen) == words32(root + "/expected-copy-clip.bgra"), "Source/destination clipped subrectangle has correct anchors and untouched margins");
    }
}
int fired = 0;
void callback(int, void*) { ++fired; }
void overlaysAndTimers(const std::string& root)
{
    Fixture f(root);
    pb::time_ticks = 100; pb::time_now = .1f; nudge::nudge_count = 7;
    timer::init(16); const auto id = timer::set(.25f, nullptr, callback);
    check(id != 0 && hq_render::Enable(true), "Pending real engine timer and HQ initialized");
    const int overlayOwner = 1;
    {
        hq_render::OverlayScope overlay(&overlayOwner);
        gdrv::copy_bitmap_w_transparency(&f.Screen, 2, 1, 1, 2, &f.Glyph, 0, 0);
    }
    hq_render::BeginRebuild(); hq_render::ReplayOverlays();
    auto expected = std::vector<uint32_t>(12, 0); expected[9] = 0x0200FF00;
    check(colors(hq_render::ScreenPixelsForTest()) == expected, "Score-style transparent overlay replays on rebuilt canvas");
    {
        hq_render::OverlayScope overlay(&overlayOwner);
        gdrv::fill_bitmap(&f.Screen, 2, 1, 1, 2, ColorRgba(0x02000044));
    }
    hq_render::Enable(false); hq_render::Enable(true); hq_render::BeginRebuild(); hq_render::ReplayOverlays();
    expected[9] = expected[10] = 0x02000044;
    check(colors(hq_render::ScreenPixelsForTest()) == expected, "Mode switch replays only latest overlay group, including erased text");
    check(pb::time_ticks == 100 && pb::time_now == .1f && nudge::nudge_count == 7 && fired == 0, "Rebuild/overlay/mode switches preserve simulation time, nudge and pending callback");
    pb::time_ticks = 351; timer::check();
    check(fired == 1, "Pending real timer still fires once at its original deadline");
    timer::uninit(); hq_render::ForgetOverlay(&overlayOwner);
}
void rejectedPacks(const std::string& root)
{
    const char* names[]{"bad-magic", "bad-footer", "bad-source-sha", "bad-original-bgra-sha", "bad-anchor", "bad-width", "bad-resolution", "bad-key", "bad-payload-length", "overflow-payload", "overflow-width", "overflow-scale", "overflow-entry-count", "zero-scale", "truncated-header", "truncated-rgb", "trailing-data", "missing-entry", "duplicate-entry", "file-does-not-exist"};
    Fixture f(root);
    const auto original = raw(f.Screen);
    for (const auto name : names) {
        if (!hq_render::Enable(true)) throw std::runtime_error("Valid control pack did not recover before rejection test");
        const bool accepted = hq_render::LoadPackForTest(root + "/" + name + ".pbhq");
        check(!accepted && !hq_render::Enabled() && !hq_render::Error().empty() && raw(f.Screen) == original, std::string("Malformed/missing pack falls back atomically: ") + name);
    }
    check(hq_render::Enable(true), "Valid pack recovers after malformed set without restart");
}
void initializeGame(const std::string& assets)
{
    ImGui::CreateContext(); winmain::ImIO = &ImGui::GetIO();
    winmain::ImIO->IniFilename = nullptr; winmain::ImIO->DisplaySize = ImVec2(640, 512);
    pinball_platform::Initialize(); options::InitPrimary();
    options::Options.Language.V = "en"; options::Options.FontFileName.V = assets + "/fonts/NotoSansKR.ttf";
    font_selection::LoadFonts(); ImGui_Render_Init(winmain::Renderer); ImGui_ImplSDL2_InitForSDLRenderer(winmain::MainWindow, winmain::Renderer);
    const auto game = assets + "/game/"; pb::SelectDatFile({game.c_str()}); options::InitSecondary(); Sound::Init(false, 8, false, 0);
    if (pb::init()) throw std::runtime_error("Actual normalized DAT initialization failed");
    fullscrn::init(); pb::reset_table(); pb::firsttime_setup(); pb::replay_level(false); render::update();
}
void shutdownGame()
{
    hq_render::Enable(false); pb::uninit(); ImGui_Render_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); winmain::ImIO = nullptr;
}
void realData(const std::string& assets, const std::string& pack)
{
    initializeGame(assets);
    score::set(pb::MainTable->CurScoreStruct, 12345); score::update(pb::MainTable->CurScoreStruct);
    pb::InfoTextBox->Display("HQ1", .25f);
    const auto baseline = raw(*render::vscreen); const auto ticks = pb::time_ticks; const auto tableScore = pb::MainTable->CurScore;
    const auto textTimer = pb::InfoTextBox->Timer;
    std::vector<std::array<int, 7>> metadata;
    for (auto group : pb::record_table->Groups) for (int r = 0; r < 3; ++r) if (auto b = group->GetBitmap(r)) metadata.push_back({{group->GroupId, int(b->Resolution), b->Width, b->Height, b->Stride, b->XPosition, b->YPosition}});
    check(hq_render::LoadPackForTest(pack), "Complete normalized real-DAT original RGB 1x pack loads");
    check(hq_render::Scale() == 1 && colors(hq_render::ScreenPixelsForTest()) == baseline, "Real DAT 1x enable initially preserves every BGRA screen pixel");
    render::RebuildHighQuality();
    check(colors(hq_render::ScreenPixelsForTest()) == baseline && raw(*render::vscreen) == baseline, "Real DAT full rebuild includes actual score digits/text and matches original 1x BGRA");
    check(pb::time_ticks == ticks && pb::MainTable->CurScore == tableScore && pb::MainTable->CurScoreStruct->Score == 12345 && pb::InfoTextBox->Timer == textTimer, "Low-level pack/rebuild preserves actual score, pending message timer and simulation time before UI pause");
    render::shift(2, -1); render::PresentVScreen(); render::shift(-2, 1);
    pb::InputDown({InputTypes::Keyboard, SDLK_SPACE}); pb::InputDown({InputTypes::Keyboard, SDLK_x});
    check(render::ApplyDisplayMode(false) && winmain::single_step && !pb::IsNudgeHeldByPhysicalInput() && !pb::MainTable->Plunger->PullbackStartedFlag, "Actual UI mode switch pauses and releases held inputs without implicit resume");
    hq_render::LoadPackForTest(pack); render::RebuildHighQuality();
    std::vector<std::array<int, 7>> after;
    for (auto group : pb::record_table->Groups) for (int r = 0; r < 3; ++r) if (auto b = group->GetBitmap(r)) after.push_back({{group->GroupId, int(b->Resolution), b->Width, b->Height, b->Stride, b->XPosition, b->YPosition}});
    const bool stockPauseMessage = pb::InfoTextBox->CurrentMessage && !strcmp(pb::InfoTextBox->CurrentMessage->Text, pb::get_rc_string(Msg::STRING123));
    check(after == metadata && pb::time_ticks == ticks && pb::MainTable->CurScore == tableScore && pb::MainTable->CurScoreStruct->Score == 12345 && pb::InfoTextBox->Timer == -1 && stockPauseMessage && winmain::single_step, "UI transition preserves logical invariants and the stock paused message/timer contract");
    check(colors(hq_render::ScreenPixelsForTest()) == raw(*render::vscreen), "Real DAT 1x original composition remains exact after paused mode transition");
    shutdownGame();
}
}

// This is the sole explicitly approved friend, declared by the renderer owner.
// The production scheduler/loop body is unchanged; every iteration below calls it.
struct PinballHqLoopProbe
{
    struct Sample { double LoopMs, RequestedDt; bool Simulated, Rendered; };
    static void Initialize()
    {
        winmain::UpdateFrameRate();
        winmain::bQuit = false;
        winmain::updateCounter = winmain::frameCounter = 0;
        winmain::frameStart = winmain::prevTime = winmain::Clock::now();
        winmain::UpdateToFrameCounter = winmain::UpdateToFrameRatio;
        winmain::sleepRemainder = winmain::DurationMs(0);
        winmain::frameDuration = winmain::TargetFrameTime;
        winmain::has_focus = winmain::activated = true;
        winmain::mouse_down = 0;
        winmain::no_time_loss = false;
    }
    static Sample Pump()
    {
        const double dt = winmain::frameDuration.count();
        const auto updates = winmain::updateCounter, frames = winmain::frameCounter;
        const auto begin = SDL_GetPerformanceCounter();
        winmain::MainLoopIteration();
        const auto end = SDL_GetPerformanceCounter();
        if (winmain::bQuit || !winmain::has_focus || winmain::single_step)
            throw std::runtime_error("Performance session interrupted, unfocused or unexpectedly paused");
        return {1000.0 * double(end - begin) / SDL_GetPerformanceFrequency(), dt,
            winmain::updateCounter != updates, winmain::frameCounter != frames};
    }
};

namespace {
std::string jsonString(const std::string& input)
{
    std::string output = "\"";
    for (char c : input) {
        if (c == '\\' || c == '\"') output += '\\';
        if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else output += c;
    }
    return output + '"';
}
void stats(std::ostream& output, std::vector<double> values)
{
    if (values.empty()) { output << "null"; return; }
    std::sort(values.begin(), values.end());
    double sum = 0; for (auto value : values) sum += value;
    const auto percentile = [&](double p) { return values[std::min(values.size() - 1, size_t(std::ceil(p * values.size()) - 1))]; };
    output << "{\"count\":" << values.size() << ",\"mean\":" << sum / values.size()
        << ",\"p95\":" << percentile(.95) << ",\"p99\":" << percentile(.99) << ",\"max\":" << values.back() << '}';
}
std::pair<uint64_t, uint64_t> memoryUsage()
{
#ifdef _WIN32
    auto module = LoadLibraryA("psapi.dll");
    if (module) {
        using ReadMemory = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
        auto read = reinterpret_cast<ReadMemory>(GetProcAddress(module, "GetProcessMemoryInfo"));
        PROCESS_MEMORY_COUNTERS values{}; values.cb = sizeof values;
        const bool ok = read && read(GetCurrentProcess(), &values, sizeof values);
        FreeLibrary(module);
        if (ok) return {values.WorkingSetSize, values.PeakWorkingSetSize};
    }
#endif
    return {0, 0};
}
struct CaptureInfo
{
    std::string File;
    int SimulationMs, Scale, Front = 0, Masked = 0, Ties = 0, ActiveBalls = 0;
    int LeftFlag = 0, RightFlag = 0;
};
CaptureInfo captureBoard(const std::string& file, int elapsed)
{
    const int scale = hq_render::Enabled() ? hq_render::Scale() : 1;
    const int width = render::vscreen->Width * scale, height = render::vscreen->Height * scale;
    const auto pixels = hq_render::ScreenPixelsForTest();
    std::ofstream image(file, std::ios::binary);
    image << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto color = pixels ? (*pixels)[y * width + x] : render::vscreen->BmpBufPtr1[y * render::vscreen->Stride + x];
        const char rgb[]{char(color.GetRed()), char(color.GetGreen()), char(color.GetBlue())}; image.write(rgb, 3);
    }
    if (!image) throw std::runtime_error("Cannot write representative image");
    CaptureInfo info; info.File = file; info.SimulationMs = elapsed; info.Scale = scale;
    info.LeftFlag = int(pb::MainTable->FlipperL->FlipperEdge->FlipperFlag);
    info.RightFlag = int(pb::MainTable->FlipperR->FlipperEdge->FlipperFlag);
    const auto depth = hq_render::DepthPixelsForTest();
    for (const auto ball : pb::MainTable->BallList) if (ball->ActiveFlag && ball->RenderSprite && ball->RenderSprite->Bmp) {
        ++info.ActiveBalls;
        if (!depth) continue;
        const auto sprite = ball->RenderSprite; const auto bmp = sprite->Bmp;
        for (int y = 0; y < bmp->Height; ++y) for (int x = 0; x < bmp->Width; ++x) {
            const int dx = sprite->BmpRect.XPosition + x, dy = sprite->BmpRect.YPosition + y;
            if (!bmp->BmpBufPtr1[y * bmp->Stride + x].Color || dx < 0 || dy < 0 || dx >= render::vscreen->Width || dy >= render::vscreen->Height) continue;
            const auto z = (*depth)[dy * scale * width + dx * scale];
            if (z > sprite->Depth) ++info.Front;
            else { ++info.Masked; if (z == sprite->Depth) ++info.Ties; }
        }
    }
    return info;
}
void performance(const std::string& assets, const std::string& pack, const std::string& outputPath)
{
    initializeGame(assets);
    options::Options.UpdatesPerSecond.V = 120; options::Options.FramesPerSecond.V = 60;
    options::Options.UncappedUpdatesPerSecond = false;
    SDL_RendererInfo renderer{}; SDL_GetRendererInfo(winmain::Renderer, &renderer);
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create performance JSON");
    const auto packBytes = bytes(pack);
    output << std::setprecision(10) << "{\"renderer\":" << jsonString(renderer.name ? renderer.name : "unknown")
        << ",\"accelerated\":" << ((renderer.flags & SDL_RENDERER_ACCELERATED) ? "true" : "false")
        << ",\"hqPackSha256\":" << jsonString(hq_render::Sha256Hex(packBytes.data(),packBytes.size()))
        << ",\"requestedUPS\":120,\"requestedFPS\":60,\"phaseSeconds\":30,\"notes\":["
        << jsonString("Exact production MainLoopIteration and its requested pb::frame(dt); full-loop wall time includes its existing sleep.") << ','
        << jsonString("Input sequence uses simulation elapsed time and new-game reset per phase. Original engine RNG stream continues; trajectories are not asserted bit-identical. Actual simulation advancement/counts are reported, never forced.") << ','
        << jsonString("Representative PPM capture adds harness work after marked iterations; next dt is marked capture-adjacent. No scheduler clock/dt rewrite during a phase.") << ','
        << jsonString("Working-set peak is process lifetime peak at each phase end, not resettable phase-only peak.") << "],\"phases\":[\n";
    struct Action { int Ms; SDL_Keycode Key; bool Down; };
    const Action sequence[]{
        {0,SDLK_SPACE,true},{1600,SDLK_SPACE,false},
        {2300,SDLK_z,true},{2550,SDLK_SLASH,true},{2800,SDLK_z,false},{3100,SDLK_SLASH,false},
        {4200,SDLK_x,true},{4260,SDLK_x,false},{5800,SDLK_PERIOD,true},{5860,SDLK_PERIOD,false},
        {7400,SDLK_UP,true},{7460,SDLK_UP,false},
        {9000,SDLK_z,true},{9400,SDLK_z,false},{10500,SDLK_SLASH,true},{10900,SDLK_SLASH,false},
        {15000,SDLK_SPACE,true},{16600,SDLK_SPACE,false},
        {18500,SDLK_z,true},{18900,SDLK_z,false},{21000,SDLK_SLASH,true},{21400,SDLK_SLASH,false}};
    const int captureAt[]{1700,2500,3300,4300,5900,7500,15000,24000};
    std::vector<std::array<int,7>> originalMetadata;
    for (auto group : pb::record_table->Groups) for (int r = 0; r < 3; ++r) if (auto b = group->GetBitmap(r)) originalMetadata.push_back({{group->GroupId,int(b->Resolution),b->Width,b->Height,b->Stride,b->XPosition,b->YPosition}});
    try {
        for (int phase = 0; phase < 3; ++phase) {
            const bool high = phase == 1;
            const std::string label = phase == 0 ? "original-before" : (high ? "hq-2x" : "original-after");
            hq_render::Enable(false); options::Options.HighQuality = false;
            pb::reset_table(); winmain::new_game(); render::update();
            if (high) {
                if (!hq_render::LoadPackForTest(pack) || hq_render::Scale() != 2) throw std::runtime_error("Selected complete 2x pack failed");
                render::RebuildHighQuality(); options::Options.HighQuality = true;
                check(hq_render::ScreenPixelsForTest() && hq_render::ScreenPixelsForTest()->size() == size_t(render::vscreen->Width * render::vscreen->Height * 4), "Actual 2x transition canvas has doubled display dimensions with logical screen retained");
            }
            const int startTicks = pb::time_ticks;
            std::vector<CaptureInfo> captures;
            if (high) captures.push_back(captureBoard(outputPath + ".hq-transition.ppm", 0));
            SDL_Event event{}; while (SDL_PollEvent(&event)) {}
            PinballHqLoopProbe::Initialize(); hq_render::Measure(true);
            std::vector<double> loops, dt, adjacentDt, cleanDt, renderedLoops;
            size_t actionIndex = 0, captureIndex = 0; int captureAdjacent = 0;
            const auto begin = SDL_GetPerformanceCounter(); bool first = true;
            while (double(SDL_GetPerformanceCounter() - begin) / SDL_GetPerformanceFrequency() < 30.0) {
                const int elapsed = pb::time_ticks - startTicks;
                while (actionIndex < sizeof(sequence)/sizeof(sequence[0]) && elapsed >= sequence[actionIndex].Ms) {
                    const auto action = sequence[actionIndex++];
                    if (action.Down) pb::InputDown({InputTypes::Keyboard, action.Key}); else pb::InputUp({InputTypes::Keyboard, action.Key});
                }
                const auto sample = PinballHqLoopProbe::Pump(); loops.push_back(sample.LoopMs);
                if (sample.Rendered) renderedLoops.push_back(sample.LoopMs);
                if (sample.Simulated) { dt.push_back(sample.RequestedDt); (captureAdjacent ? adjacentDt : cleanDt).push_back(sample.RequestedDt); }
                if (captureAdjacent) --captureAdjacent;
                if (high && first) { captures.push_back(captureBoard(outputPath + ".hq-next-frame.ppm", pb::time_ticks-startTicks)); captureAdjacent = 2; }
                first = false;
                if (captureIndex < sizeof(captureAt)/sizeof(captureAt[0]) && pb::time_ticks-startTicks >= captureAt[captureIndex]) {
                    captures.push_back(captureBoard(outputPath + "." + label + "." + std::to_string(captureAt[captureIndex++]) + ".ppm", pb::time_ticks-startTicks)); captureAdjacent = 2;
                }
            }
            const double seconds = double(SDL_GetPerformanceCounter() - begin) / SDL_GetPerformanceFrequency();
            const auto memory = memoryUsage(); const auto profile = hq_render::Samples();
            std::vector<double> composition, upload;
            for (const auto frame : profile) { composition.push_back(frame.CompositionMs); upload.push_back(frame.UploadMs); }
            std::vector<std::array<int,7>> after;
            for (auto group : pb::record_table->Groups) for (int r = 0; r < 3; ++r) if (auto b = group->GetBitmap(r)) after.push_back({{group->GroupId,int(b->Resolution),b->Width,b->Height,b->Stride,b->XPosition,b->YPosition}});
            check(after == originalMetadata && (!high || hq_render::Enabled()), "Measured phase retained logical bitmap metadata and intended renderer: " + label);
            check(actionIndex == sizeof(sequence)/sizeof(sequence[0]), "Measured phase delivered the complete fixed input sequence: " + label);
            if (phase) output << ",\n";
            output << "{\"label\":" << jsonString(label) << ",\"wallSeconds\":" << seconds << ",\"simulationMs\":" << pb::time_ticks-startTicks
                << ",\"inputActionsDelivered\":" << actionIndex << ",\"inputActionsExpected\":" << sizeof(sequence)/sizeof(sequence[0])
                << ",\"fullLoopMs\":"; stats(output,loops); output << ",\"renderedIterationLoopMs\":"; stats(output,renderedLoops);
            output << ",\"actualRequestedSimulationDtMs\":"; stats(output,dt); output << ",\"captureAdjacentDtMs\":"; stats(output,adjacentDt);
            output << ",\"otherSimulationDtMs\":"; stats(output,cleanDt); output << ",\"compositionMs\":"; stats(output,composition); output << ",\"uploadMs\":"; stats(output,upload);
            output << ",\"workingSetBytes\":" << memory.first << ",\"processLifetimePeakWorkingSetBytes\":" << memory.second << ",\"captures\":[";
            for (size_t i=0;i<captures.size();++i) { const auto& capture=captures[i]; if(i)output<<','; output << "{\"path\":" << jsonString(capture.File) << ",\"simulationMs\":" << capture.SimulationMs << ",\"scale\":" << capture.Scale << ",\"activeBalls\":" << capture.ActiveBalls << ",\"ballFrontPixels\":" << capture.Front << ",\"ballMaskedPixels\":" << capture.Masked << ",\"ballDepthTies\":" << capture.Ties << ",\"leftFlipperFlag\":" << capture.LeftFlag << ",\"rightFlipperFlag\":" << capture.RightFlag << '}'; }
            output << "]}"; output.flush();
            printf("PERFORMANCE phase=%s wall=%.3f sim_ms=%d updates=%zu rendered=%zu inputs=%zu/%zu\n", label.c_str(),seconds,pb::time_ticks-startTicks,dt.size(),renderedLoops.size(),actionIndex,sizeof(sequence)/sizeof(sequence[0]));
            pinball_platform::CancelInputs(); hq_render::Measure(false);
        }
        output << "\n]}\n"; output.close();
    } catch (...) { pinball_platform::CancelInputs(); hq_render::Measure(false); shutdownGame(); throw; }
    shutdownGame();
}
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
    check(true, message);
}
TBall* activeBall()
{
    for (auto ball : pb::MainTable->BallList)
        if (ball->ActiveFlag && ball->RenderSprite && ball->RenderSprite->Bmp) return ball;
    return nullptr;
}
void advance(int milliseconds)
{
    for (int elapsed=0;elapsed<milliseconds;elapsed+=8) { pb::frame(8); pinball_platform::AfterSimulation(); }
}
std::vector<double> worldState()
{
    std::vector<double> values{double(pb::time_ticks),pb::time_now,pb::time_next,pb::time_ticks_remainder,double(pb::MainTable->CurScore),double(pb::InfoTextBox->Timer),nudge::nudge_count};
    for (auto ball : pb::MainTable->BallList) {
        values.insert(values.end(), {double(ball->ActiveFlag),ball->Position.X,ball->Position.Y,ball->Position.Z,ball->Direction.X,ball->Direction.Y,ball->Direction.Z,ball->Speed,ball->Radius});
        if (ball->RenderSprite) { const auto r=ball->RenderSprite; values.insert(values.end(),{double(r->BmpRect.XPosition),double(r->BmpRect.YPosition),double(r->BmpRect.Width),double(r->BmpRect.Height),double(r->DirtyRect.XPosition),double(r->DirtyRect.YPosition),double(r->DirtyRect.Width),double(r->DirtyRect.Height)}); }
    }
    return values;
}
void blockExact(const std::string& boundary)
{
    const auto expected=expanded(raw(*render::vscreen),render::vscreen->Width,render::vscreen->Height,2);
    const auto actual=colors(hq_render::ScreenPixelsForTest());
    if (expected!=actual) {
        size_t first=0; while(first<std::min(expected.size(),actual.size())&&expected[first]==actual[first])++first;
        printf("INFO block_mismatch boundary=%s expected_size=%zu actual_size=%zu first=%zu expected=%08x actual=%08x\n",boundary.c_str(),expected.size(),actual.size(),first,first<expected.size()?expected[first]:0,first<actual.size()?actual[first]:0);
        throw std::runtime_error("Actual active-ball nearest2x block-exact mismatch: "+boundary);
    }
}
void activeBallTransitions(const std::string& assets,const std::string& reference,const std::string& selected,const std::string& outputPath)
{
    initializeGame(assets);
    std::vector<CaptureInfo> captures;
    int exactFrames=0,front=0,masked=0;
    auto capture=[&](const std::string& label) {
        auto image=captureBoard(outputPath+"."+label+".ppm",pb::time_ticks);
        front+=image.Front;masked+=image.Masked;captures.push_back(image);
    };
    try {
        hq_render::Enable(false); options::Options.HighQuality=false;
        pb::reset_table(); winmain::new_game(); render::update();
        const int preparedAt=pb::time_ticks;
        while(!activeBall()&&pb::time_ticks-preparedAt<9000) advance(8);
        auto ball=activeBall(); require(ball!=nullptr,"Actual game spawned a visible active ball through normal simulation");
        const auto origin=ball->Position;
        pb::InputDown({InputTypes::Keyboard,SDLK_SPACE}); advance(1600); pb::InputUp({InputTypes::Keyboard,SDLK_SPACE});
        int waited=0;
        while(waited<2000&&(ball->Speed<=1.f||(std::abs(ball->Position.X-origin.X)+std::abs(ball->Position.Y-origin.Y)<.001f))) { advance(8);waited+=8; }
        require(ball->ActiveFlag&&ball->Speed>1.f&&(std::abs(ball->Position.X-origin.X)+std::abs(ball->Position.Y-origin.Y)>.001f),"Normal held/released plunger launches the actual active ball before 2x transition");
        capture("moving-original");
        auto before=worldState();
        require(hq_render::LoadPackForTest(reference)&&hq_render::Scale()==2,"Complete original-RGB nearest2x reference loads with active moving ball");
        render::RebuildHighQuality();
        require(worldState()==before,"2x rebuild preserves actual moving-ball state, source rectangles, score and timers");
        blockExact("transition-with-moving-ball");capture("reference-transition");
        const auto transitionPosition=ball->Position;
        advance(8);blockExact("next-update-unpaint");++exactFrames;capture("reference-next-update");
        require(ball->ActiveFlag&&(ball->Position.X!=transitionPosition.X||ball->Position.Y!=transitionPosition.Y),"Next actual simulation update moves the ball while restoring/repainting scaled backups");
        const auto leftBefore=pb::MainTable->FlipperL->FlipperEdge->CurrentAngle;
        const auto rightBefore=pb::MainTable->FlipperR->FlipperEdge->CurrentAngle;
        pb::InputDown({InputTypes::Keyboard,SDLK_z});pb::InputDown({InputTypes::Keyboard,SDLK_SLASH});
        for(int i=0;i<20;++i){advance(8);blockExact("both-flippers-extend");++exactFrames;}
        require(pb::MainTable->FlipperL->FlipperEdge->CurrentAngle!=leftBefore&&pb::MainTable->FlipperR->FlipperEdge->CurrentAngle!=rightBefore,"Both actual flippers extend during block-exact active-ball rendering");capture("reference-both-flippers-up");
        pb::InputUp({InputTypes::Keyboard,SDLK_z});pb::InputUp({InputTypes::Keyboard,SDLK_SLASH});
        for(int i=0;i<20;++i){advance(8);blockExact("both-flippers-retract");++exactFrames;}
        require(std::abs(pb::MainTable->FlipperL->FlipperEdge->CurrentAngle-leftBefore)<.001f&&std::abs(pb::MainTable->FlipperR->FlipperEdge->CurrentAngle-rightBefore)<.001f,"Both actual flippers return to original angles without scaled trails");capture("reference-both-flippers-down");
        before=worldState();const auto original=raw(*render::vscreen);
        require(hq_render::Enable(false)&&!hq_render::Enabled()&&raw(*render::vscreen)==original&&worldState()==before,"Moving-ball 2x to original preserves original pixels, physics state and timers");capture("back-to-original");
        require(hq_render::LoadPackForTest(reference),"Active-ball original to reference2x reload succeeds");render::RebuildHighQuality();blockExact("reference-reenabled");
        advance(8);blockExact("reference-reenabled-next-update");++exactFrames;capture("reference-reenabled-next-update");
        before=worldState();
        require(hq_render::LoadPackForTest(selected)&&hq_render::Scale()==2,"Selected HQ2x pack executes on the same live-ball state");render::RebuildHighQuality();
        require(worldState()==before,"Selected HQ2x rebuild preserves the same live-ball state");capture("selected-transition");
        for(int i=0;i<400;++i){advance(8);if(i%20==0){capture("selected-frame-"+std::to_string(i));if(front>0&&masked>0)break;}}
        require(hq_render::Enabled(),"Selected HQ2x remains active through actual subsequent ball updates");
        require(front>0&&masked>0,"Actual active-ball captures exercise both visible and structure-masked depth conditions");
        std::ofstream output(outputPath,std::ios::binary);
        const auto referenceBytes=bytes(reference),selectedBytes=bytes(selected);
        output<<"{\"status\":\"PASS\",\"referencePackSha256\":"<<jsonString(hq_render::Sha256Hex(referenceBytes.data(),referenceBytes.size()))<<",\"selectedPackSha256\":"<<jsonString(hq_render::Sha256Hex(selectedBytes.data(),selectedBytes.size()))<<",\"exactDynamicFrames\":"<<exactFrames<<",\"frontPixelsObserved\":"<<front<<",\"maskedPixelsObserved\":"<<masked<<",\"captures\":[";
        for(size_t i=0;i<captures.size();++i){const auto& c=captures[i];if(i)output<<',';output<<"{\"path\":"<<jsonString(c.File)<<",\"simulationMs\":"<<c.SimulationMs<<",\"scale\":"<<c.Scale<<",\"activeBalls\":"<<c.ActiveBalls<<",\"ballFrontPixels\":"<<c.Front<<",\"ballMaskedPixels\":"<<c.Masked<<'}';}
        output<<"],\"scope\":\"Actual public game/sprite APIs; no ball teleport, private ball-backup access, forced callback or scheduler change. Full nearest2x screen equality across moving-ball unpaint and both-flipper frames validates scaled restore through its result.\"}\n";
    } catch(...){pinball_platform::CancelInputs();shutdownGame();throw;}
    pinball_platform::CancelInputs();shutdownGame();
}
}
int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool profiling = argc == 7 && !strcmp(argv[4], "--performance");
    const bool activeCheck = argc == 8 && !strcmp(argv[4], "--active-ball-check");
    if (argc != 2 && argc != 4 && !profiling && !activeCheck) { printf("Usage: pinball_hq_checks <fixtures> <assets> <real-1x-pack> [--performance <2x-pack> <output.json> | --active-ball-check <reference2x-pack> <selected2x-pack> <output.json>]\n"); return 2; }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS)) return 2;
    winmain::MainWindow = SDL_CreateWindow("HQ CPU checks", 0, 0, 640, 512, SDL_WINDOW_HIDDEN);
    winmain::Renderer = (profiling||activeCheck) ? SDL_CreateRenderer(winmain::MainWindow, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (!winmain::Renderer) winmain::Renderer = SDL_CreateRenderer(winmain::MainWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!winmain::MainWindow || !winmain::Renderer) return 2;
    try {
        hashes(); composition(argv[1]); overlaysAndTimers(argv[1]); rejectedPacks(argv[1]);
        if (argc >= 4) realData(argv[2], argv[3]);
        else printf("NOT_RUN real DAT integration: assets/1x pack arguments absent\n");
        if (profiling && !failures) performance(argv[2],argv[5],argv[6]);
        if (activeCheck && !failures) activeBallTransitions(argv[2],argv[5],argv[6],argv[7]);
    } catch (const std::exception& error) { printf("ERROR %s\n", error.what()); ++failures; }
    hq_render::Shutdown(); SDL_DestroyRenderer(winmain::Renderer); SDL_DestroyWindow(winmain::MainWindow); SDL_Quit();
    printf("SUMMARY hq failures=%d\n", failures); return failures ? 1 : 0;
}
