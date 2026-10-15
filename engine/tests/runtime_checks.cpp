#include "pch.h"
#include "winmain.h"
#include "options.h"
#include "pb.h"
#include "pinball_platform.h"
#include "fullscrn.h"
#include "render.h"
#include "high_score.h"
#include "nudge.h"
#include "TPinballTable.h"
#include "TFlipper.h"
#include "TFlipperEdge.h"
#include "TPlunger.h"
#include "Sound.h"
#include "font_selection.h"
#include "Help.h"
#include "translations.h"
#include "TTextBox.h"
#include <fstream>
#include <limits>

namespace {
int failures = 0;
void check(bool condition, const char* name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}
void finger(Uint32 type, SDL_FingerID id, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    event.tfinger.fingerId = id;
    event.tfinger.x = x;
    event.tfinger.y = y;
    winmain::event_handler(&event);
}
void controlFinger(Uint32 type, SDL_FingerID id, int index)
{
    int width, height;
    SDL_GetWindowSize(winmain::MainWindow, &width, &height);
    const auto rect = pinball_platform::GetControlLayout().Buttons[index];
    finger(type, id, (rect.x + rect.w / 2.0f) / width, (rect.y + rect.h / 2.0f) / height);
}
void drainHostEvents()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) winmain::event_handler(&event);
}
void simulate(int milliseconds)
{
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 8)
    {
        if (!winmain::single_step) pb::frame(8);
        pinball_platform::AfterSimulation();
    }
}
void lifecycleSessionCheck()
{
    platform_reset_input_session(42);
    drainHostEvents();
    const auto sessionTicks = pb::time_ticks;
    check(!winmain::single_step, "SESSION initial reset preserves running state without implicit resume");
    pb::InputDown({InputTypes::Keyboard, SDLK_z});
    pb::InputDown({InputTypes::Keyboard, SDLK_SPACE});
    pb::InputDown({InputTypes::Keyboard, SDLK_PERIOD});
    simulate(16);
    const auto cumulativeTilt = nudge::nudge_count;
    const auto currentScore = pb::MainTable->CurScore;
    const auto resetTicks = pb::time_ticks;
    platform_reset_input_session(43);
    drainHostEvents();
    check(!pb::MainTable->Plunger->PullbackStartedFlag && !pb::IsNudgeHeldByPhysicalInput() &&
        !nudge::nudged_left && pb::MainTable->FlipperL->FlipperEdge->FlipperFlag == MessageCode::TFlipperRetract,
        "SESSION newer reset clears real key/plunger/nudge holds");
    check(nudge::nudge_count == cumulativeTilt && pb::MainTable->CurScore == currentScore &&
        pb::time_ticks == resetTicks && resetTicks > sessionTicks,
        "SESSION reset preserves accumulated Tilt, score and simulation time");
    platform_notify_pause(1, 42);
    drainHostEvents();
    check(!winmain::single_step, "SESSION old pause cannot pause newer active session");
    platform_notify_pause(1, 44);
    drainHostEvents();
    simulate(64);
    check(winmain::single_step && pb::time_ticks == resetTicks, "SESSION current pause freezes actual simulation");
    platform_notify_pause(0, 43);
    drainHostEvents();
    check(winmain::single_step, "SESSION old resume cannot unpause newer session");
    platform_reset_input_session(45);
    drainHostEvents();
    check(winmain::single_step, "SESSION reset while paused does not implicitly resume");
    platform_notify_pause(0, 45);
    drainHostEvents();
    check(!winmain::single_step && !pb::MainTable->Plunger->PullbackStartedFlag && !pb::IsNudgeHeldByPhysicalInput(),
        "SESSION explicit same-session resume does not recreate held input");
    const SDL_Keycode keys[]{SDLK_x, SDLK_PERIOD, SDLK_UP};
    for (int direction = 0; direction < 3; ++direction) {
        pb::InputDown({InputTypes::Keyboard, keys[direction]});
        check(bool(nudge::nudged_right) == (direction == 0) && bool(nudge::nudged_left) == (direction == 1) &&
            bool(nudge::nudged_up) == (direction == 2) && pb::IsNudgeHeldByPhysicalInput(),
            "NUDGE keyboard direction retains original physical mapping");
        pb::InputUp({InputTypes::Keyboard, keys[direction]});
        check(!nudge::nudged_right && !nudge::nudged_left && !nudge::nudged_up && !pb::IsNudgeHeldByPhysicalInput(),
            "NUDGE keyboard release clears actual direction");
        const int button = direction == 0 ? 3 : direction == 1 ? 4 : 5;
        controlFinger(SDL_FINGERDOWN, 200 + direction, button);
        check(bool(nudge::nudged_right) == (direction == 0) && bool(nudge::nudged_left) == (direction == 1) &&
            bool(nudge::nudged_up) == (direction == 2), "NUDGE on-screen button retains original physical mapping");
        controlFinger(SDL_FINGERUP, 200 + direction, button);
        check(!nudge::nudged_right && !nudge::nudged_left && !nudge::nudged_up && !pb::IsNudgeHeldByPhysicalInput(),
            "NUDGE screen release clears actual direction");
    }
    for (const char* language : {"en", "ko"}) {
        translations::SetCurrentLanguage(language);
        const std::string help = translations::GetTranslation(Msg::HelpTilt);
        check(help.find("Phone Tilt") == std::string::npos && help.find("phone tilt") == std::string::npos &&
            help.find("기울기") == std::string::npos && !help.empty(), "HELP bilingual nudge/Tilt guidance excludes retired phone tilt");
    }
    translations::SetCurrentLanguage("en");
}
std::string readFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
void mobileGeometryCheck(int width, int height, float scale)
{
    const auto layout = pinball_platform::CalculateLayout(width, height, scale, true, static_cast<int>(20 * scale));
    bool inside = true, separate = true, minimum = true;
    for (int index = 0; index < 7; ++index) {
        const auto& rect = layout.Buttons[index];
        inside &= rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= width && rect.y + rect.h <= height;
        minimum &= rect.w >= 48 * scale && rect.h >= 48 * scale;
        separate &= !SDL_HasIntersection(&rect, &layout.BoardBounds);
        for (int other = 0; other < index; ++other) separate &= !SDL_HasIntersection(&rect, &layout.Buttons[other]);
    }
    check(inside && separate && minimum, "MOBILE controls are inside canvas, disjoint from board and each other, and at least 48dp");
    check(layout.Buttons[0].y == layout.Buttons[1].y && layout.Buttons[0].h >= 64 * scale &&
        layout.Buttons[1].h >= 64 * scale && layout.Buttons[2].y + layout.Buttons[2].h < layout.Buttons[1].y &&
        layout.Buttons[2].x + layout.Buttons[2].w == layout.Buttons[1].x + layout.Buttons[1].w,
        "MOBILE two bottom flippers share thumb height and launcher sits above right flipper");
    check(layout.Buttons[3].x < layout.Buttons[5].x && layout.Buttons[5].x < layout.Buttons[4].x &&
        layout.Buttons[6].y + layout.Buttons[6].h <= layout.HeaderHeight,
        "MOBILE nudge visual order is left/front/right and Pause occupies separate top header");
    if (layout.Landscape) check(layout.DockHeight == static_cast<int>(std::ceil(56 * scale)) &&
        layout.BoardBounds.x > layout.Buttons[0].x + layout.Buttons[0].w,
        "MOBILE landscape reserves only nudge-row height and uses side margins for flippers");
}
void mobileActualCheck(int width, int height, const std::string& artifact)
{
    SDL_SetWindowSize(winmain::MainWindow, width, height);
    winmain::ImIO->DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    options::Options.UIScale.V = 2.625f;
    winmain::ImIO->FontGlobalScale = 2.625f;
    fullscrn::window_size_changed();
    auto layout = pinball_platform::GetControlLayout();
    if (!layout.Mobile) return; // Actual Android source branch is run by the separate policy harness.
    bool hits = true;
    for (int index = 0; index < 7; ++index) {
        const auto& rect = layout.Buttons[index];
        hits &= pinball_platform::HitControl(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f) == index;
    }
    check(hits, "MOBILE actual shared hit test identifies every displayed button center");
    auto frame = []() {
        ImGui_Render_NewFrame();
        winmain::ImIO->DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        winmain::RenderUi();
        SDL_SetRenderDrawColor(winmain::Renderer, 5, 9, 15, 255);
        SDL_RenderClear(winmain::Renderer);
        render::PresentVScreen();
        ImGui::Render();
        ImGui_Render_RenderDrawData(ImGui::GetDrawData());
    };
    frame(); frame();
    layout = pinball_platform::GetControlLayout();
    const auto menu = ImGui::FindWindowByName("##MainMenuBar");
    check(menu && menu->Pos.y + menu->Size.y <= layout.HeaderHeight &&
        menu->DC.CursorMaxPos.x < layout.Buttons[6].x,
        "MOBILE actual main-menu labels fit header without overlapping top Pause target");
    const auto destination = render::DestinationRect;
    check(destination.x >= layout.BoardBounds.x && destination.y >= layout.BoardBounds.y &&
        destination.x + destination.w <= layout.BoardBounds.x + layout.BoardBounds.w &&
        destination.y + destination.h <= layout.BoardBounds.y + layout.BoardBounds.h,
        "MOBILE actual renderer board and score panel stay within shared board reservation");
    auto surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    const bool captured = surface && SDL_RenderReadPixels(winmain::Renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) == 0 &&
        SDL_SaveBMP(surface, artifact.c_str()) == 0;
    check(captured, "MOBILE actual SDL/ImGui renderer creates readable board-controls artifact");
    if (surface) SDL_FreeSurface(surface);
    printf("MOBILE geometry %dx%d scale %.3f header %d dock %d board %d,%d,%d,%d; artifact %s\n",
        width, height, layout.TouchScale, layout.HeaderHeight, layout.DockHeight, destination.x, destination.y, destination.w, destination.h, artifact.c_str());

    controlFinger(SDL_FINGERDOWN, 501, 0);
    controlFinger(SDL_FINGERDOWN, 502, 1);
    controlFinger(SDL_FINGERDOWN, 503, 2);
    simulate(96);
    const auto leftEdge = pb::MainTable->FlipperL->FlipperEdge;
    const auto rightEdge = pb::MainTable->FlipperR->FlipperEdge;
    // MotionFlag becomes Null at the target angle while the input remains held.
    check(std::abs(leftEdge->CurrentAngle - leftEdge->AngleMax) < 0.001f &&
        std::abs(rightEdge->CurrentAngle - rightEdge->AngleMax) < 0.001f && pb::MainTable->Plunger->PullbackStartedFlag,
        "MOBILE actual touch holds both flippers and charges launcher independently");
    controlFinger(SDL_FINGERUP, 501, 0);
    simulate(8);
    check(std::abs(leftEdge->CurrentAngle - leftEdge->AngleMax) > 0.001f &&
        std::abs(rightEdge->CurrentAngle - rightEdge->AngleMax) < 0.001f && pb::MainTable->Plunger->PullbackStartedFlag,
        "MOBILE left-pointer release retains right flipper and launcher holds");
    controlFinger(SDL_FINGERUP, 503, 2);
    check(!pb::MainTable->Plunger->PullbackStartedFlag && std::abs(rightEdge->CurrentAngle - rightEdge->AngleMax) < 0.001f,
        "MOBILE launcher release ends charge without dropping another pointer");
    finger(SDL_FINGERMOTION, 502, (layout.BoardBounds.x + layout.BoardBounds.w / 2.0f) / width,
        (layout.BoardBounds.y + layout.BoardBounds.h / 2.0f) / height);
    simulate(8);
    check(std::abs(rightEdge->CurrentAngle - rightEdge->AngleMax) > 0.001f,
        "MOBILE sliding outside a button releases its held input");
    controlFinger(SDL_FINGERUP, 502, 1);
    const auto showMenu = options::Options.ShowMenu.V;
    options::Options.ShowMenu.V = false;
    fullscrn::window_size_changed();
    controlFinger(SDL_FINGERDOWN, 504, 6); controlFinger(SDL_FINGERUP, 504, 6);
    check(winmain::single_step && pinball_platform::GetControlLayout().HeaderHeight >= 56 * layout.TouchScale,
        "MOBILE top Pause stays accessible with menu hidden");
    winmain::HandleGameBinding(GameBindings::ShowControlDialog, false);
    frame(); frame();
    controlFinger(SDL_FINGERDOWN, 505, 6); controlFinger(SDL_FINGERUP, 505, 6);
    frame();
    check(winmain::single_step && options::IsControlDialogOpen(), "MOBILE modal blocks Pause resume and preserves paused game");
    const auto caption = std::string(pb::get_rc_string(Msg::KEYMAPPER_Caption)) + "###PlayerControls";
    const auto modal = ImGui::FindWindowByName(caption.c_str());
    check(modal != nullptr, "MOBILE actual controls modal remains reachable while Pause is blocked");
    if (modal) {
        const auto spacing = ImGui::GetStyle().ItemSpacing.x;
        const auto padding = ImGui::GetStyle().WindowPadding.x;
        const auto buttonWidth = (modal->Size.x - 2 * padding - 2 * spacing) / 3;
        const auto cancelX = modal->Pos.x + padding + 1.5f * buttonWidth + spacing;
        const auto cancelY = modal->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2;
        finger(SDL_FINGERDOWN, 509, cancelX / width, cancelY / height); frame();
        finger(SDL_FINGERUP, 509, cancelX / width, cancelY / height); frame();
    }
    frame();
    check(winmain::single_step, "MOBILE closing modal retains pause before explicit Resume");
    controlFinger(SDL_FINGERDOWN, 506, 6); controlFinger(SDL_FINGERUP, 506, 6);
    check(!winmain::single_step && !pb::MainTable->Plunger->PullbackStartedFlag,
        "MOBILE explicit top Resume does not recreate held input");
    options::Options.ShowMenu.V = showMenu;
    controlFinger(SDL_FINGERDOWN, 507, 0); controlFinger(SDL_FINGERDOWN, 508, 2);
    const auto oldScore = pb::MainTable->CurScore;
    SDL_SetWindowSize(winmain::MainWindow, height, width);
    SDL_Event resized{}; resized.type = SDL_WINDOWEVENT; resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    winmain::event_handler(&resized);
    check(pb::MainTable->FlipperL->FlipperEdge->FlipperFlag == MessageCode::TFlipperRetract &&
        !pb::MainTable->Plunger->PullbackStartedFlag && pb::MainTable->CurScore == oldScore,
        "MOBILE rotation recomputes layout, cancels held inputs and preserves earned score");
    winmain::ImIO->DisplaySize = ImVec2(static_cast<float>(height), static_cast<float>(width));
    frame();
}
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool imeOnly = argc == 3 && strcmp(argv[2], "--ime-only") == 0;
    const bool sessionOnly = argc == 3 && strcmp(argv[2], "--session-only") == 0;
    if ((argc != 2 && !imeOnly && !sessionOnly) || SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) return 2;
    const std::string assets = argv[1];
    winmain::MainWindow = SDL_CreateWindow("Runtime checks", 0, 0, 640, 512, SDL_WINDOW_HIDDEN);
    winmain::Renderer = SDL_CreateRenderer(winmain::MainWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!winmain::MainWindow || !winmain::Renderer) return 2;
    ImGui::CreateContext();
    winmain::ImIO = &ImGui::GetIO();
    auto pref = SDL_GetPrefPath("PinballVerification", "RuntimeChecks");
    if (!pref) return 2;
    // Each run uses an isolated file, retaining a failed run's evidence.
    const auto path = std::string(pref) + "checks-" + std::to_string(SDL_GetPerformanceCounter()) + ".ini";
    winmain::ImIO->IniFilename = path.c_str();
    winmain::ImIO->DisplaySize = ImVec2(640, 512);
    pinball_platform::Initialize();
    options::InitPrimary();
    const auto phoneScale = options::UIScaleForDensity(420);
    if (!imeOnly && !sessionOnly) {
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
    check(options::Options.UIScale.V == 2.625f, "UI scale compiled Android initialization reads SDL logical density");
#else
    check(options::Options.UIScale.V == 1.0f, "UI scale desktop first-run default remains 1");
#endif
    check(phoneScale == 2.625f && options::UIScaleForDensity(160) == 1.0f &&
        options::UIScaleForDensity(640) == 3.0f && options::UIScaleForDensity(40) == 0.5f &&
        options::UIScaleForDensity(0) == 1.0f &&
        options::UIScaleForDensity(std::numeric_limits<float>::quiet_NaN()) == 1.0f,
        "UI scale logical density and unavailable-density fallback respect saved-value range");
    check(options::get_float("Mobile density default probe", phoneScale) == phoneScale,
        "UI scale missing setting inserts density default through actual settings store");
    options::set_float(options::Options.UIScale.Name, 1.0f);
    check(options::get_float(options::Options.UIScale.Name, phoneScale) == 1.0f,
        "UI scale explicit saved 1 is preserved instead of density migration");
    options::Options.UIScale.V = 1.0f;
    options::uninit();
    ImGui::SaveIniSettingsToDisk(path.c_str());
    ImGui::DestroyContext();
    ImGui::CreateContext();
    winmain::ImIO = &ImGui::GetIO();
    winmain::ImIO->IniFilename = path.c_str();
    winmain::ImIO->DisplaySize = ImVec2(640, 512);
    options::InitPrimary();
    check(options::Options.UIScale.V == 1.0f, "UI scale saved explicit 1 survives actual disk reload");
    }
    options::Options.Language.V = "en";
    options::Options.FontFileName.V = assets + "/fonts/NotoSansKR.ttf";
    font_selection::LoadFonts();
    ImGui_Render_Init(winmain::Renderer);
    ImGui_ImplSDL2_InitForSDLRenderer(winmain::MainWindow, winmain::Renderer);
    const auto oldImeCallback = winmain::ImIO->SetPlatformImeDataFn;
    const auto oldTextInputActive = SDL_IsTextInputActive();
    pinball_platform::InitializeTextInput();
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
    check(winmain::ImIO->SetPlatformImeDataFn != oldImeCallback && !SDL_IsTextInputActive(), "IME Android installs existing platform callback with keyboard initially closed");
#else
    check(winmain::ImIO->SetPlatformImeDataFn == oldImeCallback && SDL_IsTextInputActive() == oldTextInputActive, "IME desktop callback and physical text-input state remain unchanged");
#endif
    const auto game = assets + "/game/";
    pb::SelectDatFile({game.c_str()});
    options::InitSecondary();
    Sound::Init(false, 8, false, 0);
    if (pb::init() != 0) { printf("FAIL actual game data load\n"); return 2; }
    fullscrn::init();
    pb::reset_table();
    pb::firsttime_setup();
    pb::replay_level(false);
    if (sessionOnly) {
        simulate(1200);
        lifecycleSessionCheck();
        pinball_platform::CancelInputs(); pb::uninit();
        ImGui_Render_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
        winmain::ImIO = nullptr; SDL_free(pref);
        SDL_DestroyRenderer(winmain::Renderer); SDL_DestroyWindow(winmain::MainWindow); SDL_Quit();
        printf("SUMMARY session_only=1 failures=%d\n", failures);
        return failures ? 1 : 0;
    }
    if (!imeOnly) {
    simulate(1200);

    pb::InputDown({InputTypes::Keyboard, SDLK_SPACE});
    simulate(96);
    check(pb::MainTable->Plunger->PullbackStartedFlag, "CORE-02 real plunger charges while held");
    winmain::pause(false);
    check(winmain::single_step && !pb::MainTable->Plunger->PullbackStartedFlag,
        "CORE-02/04 pause releases held plunger before freezing");
    const auto pausedTime = pb::time_ticks;
    simulate(240);
    check(pb::time_ticks == pausedTime, "CORE-04 paused simulation does not advance");
    winmain::end_pause();
    simulate(32);
    check(!pb::MainTable->Plunger->PullbackStartedFlag, "CORE-02 resume does not re-press plunger");

    // Independent pointer ids hold both flippers; releasing one preserves the other.
    controlFinger(SDL_FINGERDOWN, 11, 0);
    controlFinger(SDL_FINGERDOWN, 12, 1);
    check(pb::MainTable->FlipperL->FlipperEdge->FlipperFlag == MessageCode::TFlipperExtend &&
        pb::MainTable->FlipperR->FlipperEdge->FlipperFlag == MessageCode::TFlipperExtend,
        "INPUT-02 two independent touch flippers");
    simulate(8); // Exercise actual extension before expecting a retract transition.
    controlFinger(SDL_FINGERUP, 11, 0);
    check(pb::MainTable->FlipperL->FlipperEdge->FlipperFlag == MessageCode::TFlipperRetract &&
        pb::MainTable->FlipperR->FlipperEdge->FlipperFlag == MessageCode::TFlipperExtend,
        "INPUT-02 one release keeps second pointer held");
    platform_cancel_inputs();
    drainHostEvents();
    check(pb::MainTable->FlipperR->FlipperEdge->FlipperFlag == MessageCode::TFlipperRetract,
        "INPUT-02 host pointer cancel releases remaining touch");

    pb::InputDown({InputTypes::Keyboard, SDLK_SPACE});
    SDL_Event lost{};
    lost.type = SDL_WINDOWEVENT;
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    winmain::event_handler(&lost);
    check(!pb::MainTable->Plunger->PullbackStartedFlag, "INPUT-01 focus loss clears physical held input");
    SDL_Event gained = lost;
    gained.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    winmain::event_handler(&gained);

    // Sensor pulse tests retired with the user-removed feature; keep lifecycle and physical-input contracts.
    lifecycleSessionCheck();

    const auto oldBinding = options::Options.Key[~GameBindings::LeftFlipper].Inputs[0];
    winmain::HandleGameBinding(GameBindings::ShowControlDialog, false);
    options::Options.Key[~GameBindings::LeftFlipper].Inputs[0] = {InputTypes::Keyboard, SDLK_a};
    options::SaveIfChanged();
    options::CancelControlDialog();
    check(options::Options.Key[~GameBindings::LeftFlipper].Inputs[0] == oldBinding,
        "INPUT-01 key edit cancel survives immediate-save boundary");
    winmain::end_pause();

    auto controlLayoutCheck = [](int width, int height, float scale) {
        SDL_SetWindowSize(winmain::MainWindow, width, height);
        winmain::ImIO->DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        options::Options.UIScale.V = scale;
        winmain::ImIO->FontGlobalScale = scale;
        winmain::HandleGameBinding(GameBindings::ShowControlDialog, false);
        bool foregroundSuppressed = true, stripRestored = false;
        auto controlFrame = [&]() {
            ImGui_Render_NewFrame();
            winmain::ImIO->DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            options::RenderControlDialog();
            auto foreground = ImGui::GetForegroundDrawList();
            const auto before = foreground->VtxBuffer.Size;
            pinball_platform::RenderControls();
            if (options::IsControlDialogOpen()) foregroundSuppressed &= foreground->VtxBuffer.Size == before;
            else stripRestored |= foreground->VtxBuffer.Size > before;
            ImGui::Render();
        };
        controlFrame(); controlFrame(); controlFrame();
        const auto caption = std::string(pb::get_rc_string(Msg::KEYMAPPER_Caption)) + "###PlayerControls";
        auto window = ImGui::FindWindowByName(caption.c_str());
        check(window && window->Pos.x >= 0 && window->Pos.y >= 0 &&
            window->Pos.x + window->Size.x <= width && window->Pos.y + window->Size.y <= height,
            "LAYOUT-01 actual control dialog fits canvas bounds");
        bool scrolls = false;
        if (window)
            for (auto child : window->DC.ChildWindows) scrolls |= child->ScrollMax.y > 0;
        check(scrolls, "INPUT-02 controls body scrolls through all bindings");
        auto body = window && !window->DC.ChildWindows.empty() ? window->DC.ChildWindows[0] : nullptr;
        auto table = body ? ImGui::TableFindByID(body->GetID("Controls")) : nullptr;
        const auto compact = body && body->WorkRect.GetWidth() < 520 * scale;
        check(table && table->ColumnsCount == (compact ? 2 : 4),
            "UI scale controls columns respond to available width in scaled font units");
        check(window && window->DC.CursorMaxPos.y <= window->Pos.y + window->Size.y,
            "INPUT-02 fixed confirmation footer remains inside canvas");
        if (!window) { options::CancelControlDialog(); return; }
        const auto spacing = ImGui::GetStyle().ItemSpacing.x;
        const auto padding = ImGui::GetStyle().WindowPadding.x;
        const auto buttonWidth = (window->Size.x - 2 * padding - 2 * spacing) / 3;
        const auto cancelX = window->Pos.x + padding + 1.5f * buttonWidth + spacing;
        const auto cancelY = window->DC.CursorPosPrevLine.y + ImGui::GetFrameHeight() / 2;
        const auto original = options::Options.Key[~GameBindings::LeftFlipper].Inputs[0];
        options::Options.Key[~GameBindings::LeftFlipper].Inputs[0] = {InputTypes::Keyboard, SDLK_a};
        options::SaveIfChanged();
        finger(SDL_FINGERDOWN, 991, cancelX / width, cancelY / height);
        controlFrame();
        finger(SDL_FINGERUP, 991, cancelX / width, cancelY / height);
        controlFrame();
        check(!options::IsControlDialogOpen() && options::Options.Key[~GameBindings::LeftFlipper].Inputs[0] == original,
            "INPUT-02 actual touch Cancel restores bindings and closes modal");
        check(winmain::single_step, "CORE-04 closing control modal preserves paused state");
        check(foregroundSuppressed && stripRestored, "LAYOUT-01 modal hides strip and closing restores it");
        if (options::IsControlDialogOpen()) options::CancelControlDialog();
        winmain::end_pause();
    };
    controlLayoutCheck(600, 512, 1.0f);
    controlLayoutCheck(360, 640, 1.0f);
    controlLayoutCheck(1248, 1710, phoneScale);
    controlLayoutCheck(1710, 1248, phoneScale);

    // Exercise the actual board text window separately from the external density UI.
    SDL_SetWindowSize(winmain::MainWindow, 1248, 1710);
    winmain::ImIO->DisplaySize = ImVec2(1248, 1710);
    options::Options.UIScale.V = phoneScale;
    winmain::ImIO->FontGlobalScale = phoneScale;
    fullscrn::window_size_changed();
    TTextBox* boardBox = nullptr;
    for (auto component : pb::MainTable->ComponentList)
        if (auto text = dynamic_cast<TTextBox*>(component))
            if (text->CurrentMessage) { boardBox = text; break; }
    auto savedBoardFont = boardBox ? boardBox->Font : nullptr;
    if (boardBox) boardBox->Font = nullptr; // Select the production ImGui message path.
    ImGui_Render_NewFrame();
    ImGui::NewFrame();
    if (boardBox) boardBox->DrawImGui();
    auto stripDraw = ImGui::GetForegroundDrawList();
    pinball_platform::RenderControls();
    check(pinball_platform::ControlsHeight() ==
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
        337 &&
#else
        252 &&
#endif
        std::abs(stripDraw->_Data->FontSize - 13.0f * phoneScale) < 0.001f,
        "UI scale actual dock follows its platform layout and uses 13dp labels");
    char boardName[64];
    snprintf(boardName, sizeof boardName, "TTextBox_%p", static_cast<void*>(boardBox));
    auto boardWindow = ImGui::FindWindowByName(boardName);
    auto expectedBoardScale = fullscrn::GetScreenToPinballRatio();
#ifndef PINBALL_TEST_ANDROID_BOARD_TEXT
    expectedBoardScale *= phoneScale; // Desktop's prior user scale behavior is retained.
#endif
    check(boardWindow && std::abs(boardWindow->CalcFontSize() - 13.0f * expectedBoardScale) < 0.001f,
        "UI scale actual board message font follows its platform scaling contract");
    const auto boardRect = boardBox ? fullscrn::GetScreenRectFromPinballRect(
        SDL_Rect{boardBox->OffsetX, boardBox->OffsetY, boardBox->Width, boardBox->Height}) : SDL_Rect{};
    check(boardWindow && boardWindow->Size.x == boardRect.w && boardWindow->Size.y == boardRect.h &&
        boardWindow->Pos.x == boardRect.x && boardWindow->Pos.y == boardRect.y,
        "UI scale actual board message clipping stays aligned with board rectangle");
    ImGui::Render();
    if (boardBox) boardBox->Font = savedBoardFont;
    options::Options.UIScale.V = 1.0f;
    winmain::ImIO->FontGlobalScale = 1.0f;
    mobileGeometryCheck(375, 812, 1);
    mobileGeometryCheck(390, 844, 1);
    mobileGeometryCheck(475, 651, 1);
    mobileGeometryCheck(844, 390, 1);
    mobileGeometryCheck(651, 475, 1);
    mobileGeometryCheck(240, 300, 1);
    mobileGeometryCheck(1248, 1710, phoneScale);
    mobileGeometryCheck(1710, 1248, phoneScale);
    mobileActualCheck(984, 2132, "verification/mobile-controls/portrait-375.bmp");
    mobileActualCheck(1024, 2216, "verification/mobile-controls/portrait-390.bmp");
    mobileActualCheck(1248, 1710, "verification/mobile-controls/portrait-475.bmp");
    mobileActualCheck(2132, 984, "verification/mobile-controls/landscape-812.bmp");
    mobileActualCheck(1710, 1248, "verification/mobile-controls/landscape-651.bmp");
    options::Options.UIScale.V = 1.0f;
    winmain::ImIO->FontGlobalScale = 1.0f;
    SDL_SetWindowSize(winmain::MainWindow, 640, 512);
    winmain::ImIO->DisplaySize = ImVec2(640, 512);
    }
    if (imeOnly) {
        printf("IME stage focus\n");
        SDL_SetWindowSize(winmain::MainWindow, 1248, 1710);
        winmain::ImIO->DisplaySize = ImVec2(1248, 1710);
        options::Options.UIScale.V = 2.625f;
        winmain::ImIO->FontGlobalScale = 2.625f;
        fullscrn::window_size_changed();
    }

    if (imeOnly) {
        pb::InfoTextBox->Display(pb::get_rc_string(Msg::STRING125), -1.0f);
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
        const auto expectedPrompt = std::string(pb::get_rc_string(Msg::Menu1_Game)) + " > " + translations::GetTranslation(Msg::UiNewGame);
        check(pb::InfoTextBox->CurrentMessage && expectedPrompt == pb::InfoTextBox->CurrentMessage->Text,
            "MOBILE Game Over prompt names the real Game menu and New Game action");
#else
        check(pb::InfoTextBox->CurrentMessage && !strcmp(pb::InfoTextBox->CurrentMessage->Text, pb::get_rc_string(Msg::STRING125)),
            "MOBILE desktop Game Over prompt retains the original F2 shortcut");
#endif
    }

    high_score_struct entry{{0}, 123456};
    strcpy(entry.Name, "Boundary check");
    high_score::show_and_set_high_score_dialog({entry, 0});
    auto scoreDialogFrame = []() {
        ImGui_Render_NewFrame();
        winmain::ImIO->DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        high_score::RenderHighScoreDialog();
        ImGui::Render();
        pinball_platform::RefreshTextInput();
    };
    scoreDialogFrame();
    scoreDialogFrame();
    scoreDialogFrame();
    const auto keyEvent = [](Uint32 type, SDL_Keycode code, Uint16 modifiers = KMOD_NONE) {
        SDL_Event event{}; event.type = type; event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code); event.key.keysym.mod = modifiers;
        winmain::event_handler(&event);
    };
    const auto textEvent = [](const char* text) {
        SDL_Event event{}; event.type = SDL_TEXTINPUT;
        SDL_strlcpy(event.text.text, text, sizeof event.text.text);
        winmain::event_handler(&event);
    };
    if (imeOnly) {
        const auto input = ImGui::GetInputTextState(ImGui::GetActiveID());
        check(input && ImGui::GetCurrentContext()->PlatformImeData.WantVisible, "IME actual high-score name field activates and requests platform keyboard");
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
        check(SDL_IsTextInputActive(), "IME actual high-score focus enables SDL text input through platform callback");
        SDL_SetWindowSize(winmain::MainWindow, 1248, 950);
        winmain::ImIO->DisplaySize = ImVec2(1248, 950);
        SDL_Event resized{}; resized.type = SDL_WINDOWEVENT; resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
        winmain::event_handler(&resized); scoreDialogFrame(); scoreDialogFrame();
        check(SDL_IsTextInputActive(), "IME keyboard resize preserves the focused SDL text-input session");
        SDL_StopTextInput(); // Exact native operation used by SDLActivity.onNativeKeyboardFocusLost (Back).
        winmain::event_handler(&resized); scoreDialogFrame(); scoreDialogFrame();
        check(!SDL_IsTextInputActive(), "IME Back dismiss remains closed when keyboard insets move the active caret");
        auto modal = ImGui::FindWindowByName(pb::get_rc_string(Msg::HIGHSCORES_Caption));
        check(modal && modal->Pos.y >= 0 && modal->Pos.y + modal->Size.y <= 950 && modal->DC.CursorMaxPos.y <= 950,
            "IME high-score modal and confirmation footer remain within resized viewport");
        const auto caret = ImGui::GetCurrentContext()->PlatformImeData;
        finger(SDL_FINGERDOWN, 850, (caret.InputPos.x - 4) / 1248, (caret.InputPos.y + caret.InputLineHeight / 2) / 950);
        scoreDialogFrame();
        finger(SDL_FINGERUP, 850, (caret.InputPos.x - 4) / 1248, (caret.InputPos.y + caret.InputLineHeight / 2) / 950);
        scoreDialogFrame();
        check(SDL_IsTextInputActive(), "IME explicit tap refocuses the active name field after Back dismissal");
#endif
        // SDL Android composition replaces codepoints with backspaces plus UTF-8 commits.
        printf("IME stage composing\n");
        keyEvent(SDL_KEYDOWN, SDLK_a, KMOD_CTRL); scoreDialogFrame();
        keyEvent(SDL_KEYUP, SDLK_a); scoreDialogFrame();
        textEvent("ㄱ"); scoreDialogFrame();
        keyEvent(SDL_KEYDOWN, SDLK_BACKSPACE); scoreDialogFrame();
        keyEvent(SDL_KEYUP, SDLK_BACKSPACE); scoreDialogFrame();
        textEvent("가"); scoreDialogFrame();
        const auto composed = ImGui::GetInputTextState(ImGui::GetActiveID());
        check(composed && composed->TextA.Data && strcmp(composed->TextA.Data, "가") == 0, "IME SDL composing replacement produces exact committed UTF-8 name");
        keyEvent(SDL_KEYDOWN, SDLK_SPACE); scoreDialogFrame();
        keyEvent(SDL_KEYUP, SDLK_SPACE); scoreDialogFrame();
        check(!pb::MainTable->Plunger->PullbackStartedFlag, "IME captured keyboard input never starts the game plunger");
        keyEvent(SDL_KEYDOWN, SDLK_a, KMOD_CTRL); scoreDialogFrame();
        keyEvent(SDL_KEYUP, SDLK_a); scoreDialogFrame();
        textEvent("Boundary check"); scoreDialogFrame();
        printf("IME stage confirm\n");
    }
    keyEvent(SDL_KEYDOWN, SDLK_RETURN);
    scoreDialogFrame();
    keyEvent(SDL_KEYUP, SDLK_RETURN);
    scoreDialogFrame();
    auto saved = readFile(path);
    check(saved.find("0.Score=123456") != std::string::npos &&
        saved.find("0.Name=Boundary check") != std::string::npos,
        "SAVE-01 confirmed high score immediately reaches disk");
    if (imeOnly) {
#ifdef PINBALL_TEST_ANDROID_UI_DENSITY
        check(!SDL_IsTextInputActive(), "IME confirmed dialog closes keyboard and releases SDL text input");
#endif
        check(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !pb::MainTable->Plunger->PullbackStartedFlag,
            "IME Enter confirms high score without accidental gameplay input");
        pinball_platform::SuspendTextInput();
        pinball_platform::CancelInputs(); pb::uninit();
        ImGui_Render_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
        winmain::ImIO = nullptr; SDL_free(pref);
        SDL_DestroyRenderer(winmain::Renderer); SDL_DestroyWindow(winmain::MainWindow); SDL_Quit();
        return failures ? 1 : 0;
    }
    options::Options.SoundVolume = 27;
    options::SaveIfChanged();
    check(readFile(path).find("Sound Volume=27") != std::string::npos,
        "SAVE-01 changed option reaches disk before shutdown");
    const auto score = pb::MainTable->CurScore;
    const auto player = pb::MainTable->CurrentPlayer;
    SDL_Event reset{};
    reset.type = SDL_RENDER_DEVICE_RESET;
    winmain::event_handler(&reset);
    render::PresentVScreen();
    check(render::vscreen->Texture && pb::MainTable->CurScore == score &&
        pb::MainTable->CurrentPlayer == player, "LIFECYCLE renderer reset retains live game state");
    // Read back an actually damaged INI through the same parser used on startup.
    pinball_platform::CancelInputs();
    pb::uninit();
    ImGui_Render_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    { std::ofstream damaged(path, std::ios::binary); damaged << "[Pinball][Settings]\nPlayers=broken\nVerification=0\n0.Name=Damaged\n0.Score=9000\n"; }
    ImGui::CreateContext();
    winmain::ImIO = &ImGui::GetIO();
    winmain::ImIO->IniFilename = path.c_str();
    options::InitPrimary();
    high_score::read();
    check(options::Options.Players == 1 && high_score::highscore_table[0].Score == -999,
        "SAVE-01 damaged numeric setting and checksum recover safely");
    const auto backupPattern = pinball_platform::RecoveryBackupPath();
    check(!backupPattern.empty() && readFile(backupPattern).find("Players=broken") != std::string::npos,
        "SAVE-01 damaged bytes are copied to recoverable backup");
    // File backup is verified by inspecting the same source bytes before overwrite.
    check(readFile(path).find("Players=broken") != std::string::npos,
        "SAVE-01 damaged source is retained until explicit recovered save");
    printf("Evidence INI: %s; backup glob: %s\n", path.c_str(), backupPattern.c_str());
    ImGui::DestroyContext();
    winmain::ImIO = nullptr;
    SDL_free(pref);
    SDL_DestroyRenderer(winmain::Renderer);
    SDL_DestroyWindow(winmain::MainWindow);
    SDL_Quit();
    return failures ? 1 : 0;
}
