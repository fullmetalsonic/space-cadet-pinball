#include "pch.h"
#include "pinball_platform.h"
#include "options.h"
#include "pb.h"
#include "winmain.h"
#include "fullscrn.h"
#include "translations.h"
#include "Help.h"
#include <atomic>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <climits>
#endif

namespace {
std::atomic<Uint32> hostEvent{0};
enum Command { Pause, ResetInputSession, Cancel, Flush };
uint32_t session = 0;
std::map<SDL_FingerID, GameBindings> fingers;
std::map<int, GameBindings> mouseButtons;
std::string storageNotice, recoveryBackup;
bool uiFingerActive = false;
SDL_FingerID uiFinger = 0;
std::atomic<int> englishLanguage{0};
#ifdef __EMSCRIPTEN__
int webNameSession = 0, webNameCursor = 0, webNameAnchor = 0;
bool webNameVisible = false, webNameSeen = false, webNamePending = false, webNameSubmit = false;
char webNameValue[32]{};
int webNameBoundary(int position)
{
    position = std::max(0, std::min(position, static_cast<int>(strlen(webNameValue))));
    while (position > 0 && (static_cast<unsigned char>(webNameValue[position]) & 0xc0) == 0x80) --position;
    return position;
}
#endif
#ifdef __ANDROID__
bool imeRequested = false;
void setAndroidImeData(ImGuiViewport*, ImGuiPlatformImeData* data)
{
    if (data->WantVisible) {
        SDL_Rect rect{static_cast<int>(std::floor(data->InputPos.x)), static_cast<int>(std::floor(data->InputPos.y)),
            1, std::max(1, static_cast<int>(std::ceil(data->InputLineHeight)))};
        SDL_SetTextInputRect(&rect);
        if (!imeRequested) {
            SDL_StartTextInput();
            imeRequested = true;
        }
    } else {
        if (imeRequested || SDL_IsTextInputActive()) SDL_StopTextInput();
        imeRequested = false;
    }
}
#endif

int enqueue(Command command, int value, uint32_t epoch)
{
    const auto type = hostEvent.load();
    if (!type) return 0;
    SDL_Event event{};
    event.type = type;
    event.user.code = command;
    event.user.data1 = reinterpret_cast<void*>(static_cast<intptr_t>(value));
    event.user.data2 = reinterpret_cast<void*>(static_cast<uintptr_t>(epoch));
#ifdef __EMSCRIPTEN__
    // Host JS runs on this same thread; lifecycle must finish before browser timers suspend.
    if (winmain::ImIO && pb::MainTable)
        return pinball_platform::ProcessEvent(event) ? 1 : 0;
#endif
    return SDL_PushEvent(&event) == 1;
}

int hit(float x, float y)
{
    return pinball_platform::HitControl(x, y);
}

bool press(int index)
{
    if (index == 6) {
        if (!options::IsControlDialogOpen() && !Help::IsOpen() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) winmain::pause();
        return false;
    }
    return index >= 0 && index < 6 && pb::InputBindingDown(static_cast<GameBindings>(index), "screen-button");
}
}

pinball_platform::ControlLayout pinball_platform::CalculateLayout(int width, int height, float scale, bool mobile, int menuHeight)
{
    scale = std::isfinite(scale) && scale > 0 ? scale : 1.0f;
    ControlLayout layout{};
    layout.TouchScale = scale;
    layout.Mobile = mobile;
    layout.Landscape = mobile && width > height && width / scale >= 360;
    if (!mobile) {
        layout.HeaderHeight = menuHeight;
        layout.BoardBounds = SDL_Rect{0, menuHeight, width, std::max(1, height - menuHeight)};
        return layout;
    }
    const auto dp = [scale](float value) { return static_cast<int>(std::ceil(value * scale)); };
    const auto gap = dp(4), flipperHeight = dp(64), launchHeight = dp(56), nudgeSize = dp(48);
    layout.HeaderHeight = std::max(menuHeight, dp(56));
    layout.Buttons[6] = SDL_Rect{width - gap - dp(72), gap, dp(72), dp(48)};
    const auto bottomY = height - gap - flipperHeight;
    const auto launchY = bottomY - gap - launchHeight;
    if (!layout.Landscape) {
        layout.DockHeight = gap + flipperHeight + gap + launchHeight;
        const auto flipperWidth = (width - 3 * gap) / 2;
        layout.Buttons[0] = SDL_Rect{gap, bottomY, flipperWidth, flipperHeight};
        layout.Buttons[1] = SDL_Rect{width - gap - flipperWidth, bottomY, flipperWidth, flipperHeight};
        layout.Buttons[2] = SDL_Rect{width - gap - dp(64), launchY, dp(64), launchHeight};
        layout.BoardBounds = SDL_Rect{0, layout.HeaderHeight, width,
            std::max(1, height - layout.HeaderHeight - layout.DockHeight)};
    } else {
        // Flippers and launcher occupy the side margins. Only the nudge row
        // reserves vertical space, preserving the board in short landscape views.
        const auto side = dp(96), flipperWidth = side - 2 * gap;
        layout.DockHeight = dp(56);
        layout.Buttons[0] = SDL_Rect{gap, bottomY, flipperWidth, flipperHeight};
        layout.Buttons[1] = SDL_Rect{width - gap - flipperWidth, bottomY, flipperWidth, flipperHeight};
        layout.Buttons[2] = SDL_Rect{width - gap - flipperWidth, launchY, flipperWidth, launchHeight};
        layout.BoardBounds = SDL_Rect{side, layout.HeaderHeight, std::max(1, width - 2 * side),
            std::max(1, height - layout.HeaderHeight - layout.DockHeight)};
    }
    const auto nudgeY = layout.Landscape ? height - gap - nudgeSize : launchY + (launchHeight - nudgeSize) / 2;
    const auto nudgeWidth = 3 * nudgeSize + 2 * gap;
    auto nudgeX = (width - nudgeWidth) / 2;
    if (!layout.Landscape) nudgeX = std::min(nudgeX, layout.Buttons[2].x - gap - nudgeWidth);
    // Logical ids remain left/right/front; visual order is left/front/right.
    layout.Buttons[3] = SDL_Rect{nudgeX, nudgeY, nudgeSize, nudgeSize};
    layout.Buttons[5] = SDL_Rect{nudgeX + nudgeSize + gap, nudgeY, nudgeSize, nudgeSize};
    layout.Buttons[4] = SDL_Rect{nudgeX + 2 * (nudgeSize + gap), nudgeY, nudgeSize, nudgeSize};
    return layout;
}

pinball_platform::ControlLayout pinball_platform::GetControlLayout()
{
    int width = 600, height = 512;
    if (winmain::MainWindow) SDL_GetWindowSize(winmain::MainWindow, &width, &height);
    bool mobile = false;
    float scale = options::Options.UIScale.V;
#ifdef __ANDROID__
    mobile = true;
    float densityDpi = 0;
    const auto display = SDL_GetWindowDisplayIndex(winmain::MainWindow);
    if (display >= 0 && SDL_GetDisplayDPI(display, &densityDpi, nullptr, nullptr) == 0 && densityDpi > 0 && std::isfinite(densityDpi))
        scale = densityDpi / 160.0f;
#elif defined(__EMSCRIPTEN__)
    mobile = EM_ASM_INT({ return window.matchMedia('(pointer: coarse)').matches || Math.min(window.innerWidth, window.innerHeight) <= 600; }) != 0;
    if (mobile) scale = EM_ASM_DOUBLE({ const rect = Module.canvas.getBoundingClientRect(); return rect.width > 0 ? $0 / rect.width : 1; }, width);
#endif
    return CalculateLayout(width, height, scale, mobile, options::Options.ShowMenu ? winmain::MainMenuHeight : 0);
}

int pinball_platform::HitControl(float x, float y)
{
    const auto layout = GetControlLayout();
    for (int index = 0; index < 7; ++index) {
        const auto& rect = layout.Buttons[index];
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) return index;
    }
    return -1;
}

extern "C" void platform_notify_pause(int paused, uint32_t session_id) { enqueue(Pause, paused, session_id); }
extern "C" void platform_reset_input_session(uint32_t session_id) { enqueue(ResetInputSession, 0, session_id); }
extern "C" void platform_cancel_inputs() { enqueue(Cancel, 0, 0); }
extern "C" int platform_is_english() { return englishLanguage.load(); }
extern "C" void platform_activate_audio()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (typeof SDL2 !== 'undefined' && SDL2.audioContext) SDL2.audioContext.resume(); });
#endif
}
extern "C" void platform_flush_settings() { enqueue(Flush, 0, 0); }

#ifdef __EMSCRIPTEN__
extern "C" int platform_web_name_edit(int epoch, const char* value, int cursor, int anchor)
{
    if (!webNameVisible || epoch != webNameSession || !value) return 0;
    SDL_utf8strlcpy(webNameValue, value, sizeof webNameValue);
    webNameCursor = webNameBoundary(cursor);
    webNameAnchor = webNameBoundary(anchor);
    webNamePending = true;
    return 1;
}
extern "C" void platform_web_name_submit(int epoch)
{
    if (webNameVisible && epoch == webNameSession) webNameSubmit = true;
}
void pinball_platform::BeginWebNameFrame() { webNameSeen = false; }
void pinball_platform::PrepareWebName(char* value, int size)
{
    // Also apply while ImGui's field is inactive, e.g. a browser edit followed by
    // tapping the existing Confirm button. CallbackAlways updates active state.
    if (webNamePending) SDL_utf8strlcpy(value, webNameValue, size);
}
int pinball_platform::WebNameCallback(ImGuiInputTextCallbackData* data)
{
    if (webNamePending) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, webNameValue);
        data->CursorPos = webNameCursor;
        data->SelectionStart = std::min(webNameCursor, webNameAnchor);
        data->SelectionEnd = std::max(webNameCursor, webNameAnchor);
        data->BufDirty = true;
    }
    return 0;
}
bool pinball_platform::PublishWebName(const char* value, const char* label)
{
    webNameSeen = true;
    if (!webNameVisible) {
        webNameSession = webNameSession == INT_MAX ? 1 : webNameSession + 1;
        webNameVisible = true;
    }
    const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
    const auto display = ImGui::GetIO().DisplaySize;
    EM_ASM({
        globalThis.pinballWebNameInput?.($0, UTF8ToString($1), UTF8ToString($2), $3, $4, $5, $6, $7, $8);
    }, webNameSession, value, label, minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y, display.x, display.y);
    webNamePending = false;
    const auto submit = webNameSubmit;
    webNameSubmit = false;
    return submit;
}
void pinball_platform::EndWebNameFrame(bool popupOpen)
{
    if (webNameVisible && (!popupOpen || !webNameSeen)) {
        webNameVisible = webNamePending = webNameSubmit = false;
        EM_ASM({ globalThis.pinballWebNameInput?.(0); });
    }
}
#endif

void pinball_platform::Initialize()
{
    hostEvent = SDL_RegisterEvents(1);
    if (hostEvent == static_cast<Uint32>(-1)) hostEvent = 0;
    session = 0;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
}

void pinball_platform::InitializeTextInput()
{
#ifdef __ANDROID__
    // This bundled SDL ImGui backend handles committed UTF-8 but does not open an IME.
    // Use ImGui's existing caret/visibility callback; keep native desktop IME unchanged.
    imeRequested = false;
    SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME, "0");
    ImGui::GetIO().SetPlatformImeDataFn = setAndroidImeData;
    SDL_StopTextInput();
#endif
}

void pinball_platform::RefreshTextInput()
{
#ifdef __ANDROID__
    // Android Back stops SDL text input. Do not reopen it merely because keyboard
    // insets move the caret; an explicit tap in the still-active text widget can reopen it.
    auto& data = ImGui::GetCurrentContext()->PlatformImeData;
    if (data.WantVisible && !SDL_IsTextInputActive() && ImGui::IsMouseClicked(0)) {
        imeRequested = false;
        setAndroidImeData(ImGui::GetMainViewport(), &data);
    }
#endif
}

void pinball_platform::SuspendTextInput()
{
#ifdef __ANDROID__
    SDL_StopTextInput();
    imeRequested = false;
#endif
}

void pinball_platform::CancelInputs()
{
    pb::CancelInputs();
    fingers.clear();
    mouseButtons.clear();
    if (uiFingerActive && winmain::ImIO) winmain::ImIO->AddMouseButtonEvent(0, false);
    uiFingerActive = false;
}

bool pinball_platform::ProcessEvent(const SDL_Event& event)
{
    if (event.type == hostEvent)
    {
        const auto value = static_cast<int>(reinterpret_cast<intptr_t>(event.user.data1));
        const auto epoch = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event.user.data2));
        switch (event.user.code)
        {
        case Pause:
        case ResetInputSession:
            if (epoch < session) break;
            session = epoch;
            CancelInputs();
            if (event.user.code == Pause)
            {
                if (value) winmain::pause(false);
                else winmain::end_pause();
                // Hosts send false only after the user explicitly selects resume.
                Persist();
            }
            break;
        case Cancel: CancelInputs(); break;
        case Flush: Persist(); break;
        }
        return true;
    }
    if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP)
    {
        int w, h;
        SDL_GetWindowSize(winmain::MainWindow, &w, &h);
        const auto id = event.tfinger.fingerId;
        const auto index = hit(event.tfinger.x * w, event.tfinger.y * h);
        const bool uiOpen = options::IsControlDialogOpen() || Help::IsOpen() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        auto held = fingers.find(id);
        if (held != fingers.end() && (event.type == SDL_FINGERUP || uiOpen || index != ~held->second))
        {
            pb::InputBindingUp(held->second);
            fingers.erase(held);
            held = fingers.end();
        }
        if (uiOpen || uiFingerActive || (index < 0 && held == fingers.end()))
        {
            // SDL touch-to-mouse synthesis is off; route one UI pointer explicitly.
            if (event.type == SDL_FINGERDOWN && !uiFingerActive) { uiFinger = id; uiFingerActive = true; }
            if (uiFingerActive && uiFinger == id)
            {
                winmain::ImIO->AddMousePosEvent(event.tfinger.x * w, event.tfinger.y * h);
                if (event.type == SDL_FINGERDOWN) winmain::ImIO->AddMouseButtonEvent(0, true);
                if (event.type == SDL_FINGERUP) { winmain::ImIO->AddMouseButtonEvent(0, false); uiFingerActive = false; }
            }
            return true;
        }
        if (event.type == SDL_FINGERDOWN)
        {
            const auto index = hit(event.tfinger.x * w, event.tfinger.y * h);
            if (!options::IsControlDialogOpen() && !Help::IsOpen() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && press(index))
                fingers[id] = static_cast<GameBindings>(index);
        }
        return true;
    }
    if (event.type == SDL_MOUSEMOTION && !mouseButtons.empty())
    {
        for (auto it = mouseButtons.begin(); it != mouseButtons.end();)
        {
            if (hit(event.motion.x, event.motion.y) != ~it->second)
            { pb::InputBindingUp(it->second); it = mouseButtons.erase(it); }
            else ++it;
        }
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
    {
        if (event.button.which == SDL_TOUCH_MOUSEID) return true;
        auto held = mouseButtons.find(event.button.button);
        if (event.type == SDL_MOUSEBUTTONUP && held != mouseButtons.end())
        {
            pb::InputBindingUp(held->second);
            mouseButtons.erase(held);
            return true;
        }
        const auto index = hit(event.button.x, event.button.y);
        if (index >= 0 && event.type == SDL_MOUSEBUTTONDOWN &&
            !options::IsControlDialogOpen() && !Help::IsOpen() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            if (press(index)) mouseButtons[event.button.button] = static_cast<GameBindings>(index);
            return true;
        }
    }
    return false;
}

void pinball_platform::AfterSimulation()
{
    englishLanguage = options::Options.Language.V == "en";

}

int pinball_platform::ControlsHeight()
{
    return GetControlLayout().DockHeight;
}

void pinball_platform::RenderControls()
{
    const auto layout = GetControlLayout();
    if (!layout.Mobile) return;
    if (options::IsControlDialogOpen() || Help::IsOpen() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    englishLanguage = options::Options.Language.V == "en";
    const char* labels[] = {
        translations::GetTranslation(Msg::UiLeft), translations::GetTranslation(Msg::UiRight),
        translations::GetTranslation(Msg::UiLaunch), translations::GetTranslation(Msg::UiNudgeLeft),
        translations::GetTranslation(Msg::UiNudgeRight), translations::GetTranslation(Msg::UiNudgeForward),
        translations::GetTranslation(winmain::single_step ? Msg::UiResume : Msg::UiPause) };
    auto draw = ImGui::GetForegroundDrawList();
    for (int index = 0; index < 7; ++index)
    {
        const auto rect = layout.Buttons[index];
        const auto start = ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y));
        const auto end = ImVec2(start.x + rect.w, start.y + rect.h);
        bool held = false;
        for (auto& finger : fingers) held |= ~finger.second == index;
        for (auto& button : mouseButtons) held |= ~button.second == index;
        draw->AddRectFilled(start, end, held ? IM_COL32(35, 100, 165, 255) : IM_COL32(32, 42, 54, 255), 6 * (layout.Mobile ? layout.TouchScale : 1));
        draw->PushClipRect(start, end, true);
        if (layout.Mobile && index >= 3 && index <= 5) {
            // Vector arrows remain readable without relying on font arrow glyphs.
            const auto size = 10 * layout.TouchScale;
            const auto center = ImVec2(start.x + rect.w / 2.0f, start.y + rect.h / 2.0f);
            const auto color = IM_COL32(255, 255, 255, 255);
            const auto thickness = 2 * layout.TouchScale;
            if (index == 5) {
                draw->AddLine(ImVec2(center.x, center.y + size), ImVec2(center.x, center.y - size), color, thickness);
                draw->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x - size / 2, center.y - size / 2), color, thickness);
                draw->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x + size / 2, center.y - size / 2), color, thickness);
            } else {
                const auto direction = index == 3 ? -1 : 1;
                draw->AddLine(ImVec2(center.x - direction * size, center.y), ImVec2(center.x + direction * size, center.y), color, thickness);
                draw->AddLine(ImVec2(center.x + direction * size, center.y), ImVec2(center.x + direction * size / 2, center.y - size / 2), color, thickness);
                draw->AddLine(ImVec2(center.x + direction * size, center.y), ImVec2(center.x + direction * size / 2, center.y + size / 2), color, thickness);
            }
        } else {
            const char* label = labels[index];
            if (layout.Landscape && index < 2)
                label = englishLanguage ? (index == 0 ? "Left\nFlipper" : "Right\nFlipper") : (index == 0 ? "왼쪽\n플리퍼" : "오른쪽\n플리퍼");
            const auto textSize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(start.x + (rect.w - textSize.x) / 2, start.y + (rect.h - textSize.y) / 2),
                IM_COL32(255, 255, 255, 255), label);
        }
        draw->PopClipRect();
        if (layout.Mobile && ImGui::IsMouseHoveringRect(start, end)) ImGui::SetTooltip("%s", labels[index]);
    }
}

void pinball_platform::Persist()
{
    if (!ImGui::GetCurrentContext() || !winmain::ImIO || !winmain::ImIO->IniFilename) return;
    options::uninit();
    ImGui::SaveIniSettingsToDisk(winmain::ImIO->IniFilename);
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (typeof window.pinballPersistenceFlush === 'function') window.pinballPersistenceFlush(); });
#endif
}

void pinball_platform::BackupDamagedSettings(const char* reason)
{
    if (!winmain::ImIO || !winmain::ImIO->IniFilename || !storageNotice.empty()) return;
    const auto path = std::string(winmain::ImIO->IniFilename);
    const auto backup = path + ".damaged-" + std::to_string(SDL_GetTicks()) + ".bak";
    auto source = fopenu(path.c_str(), "rb");
    auto target = source ? fopenu(backup.c_str(), "wb") : nullptr;
    if (target)
    {
        char bytes[4096];
        size_t count;
        while ((count = fread(bytes, 1, sizeof bytes, source))) fwrite(bytes, 1, count, target);
        fclose(target);
    }
    if (source) fclose(source);
    recoveryBackup = target ? backup : "";
    storageNotice = translations::GetTranslation(Msg::UiSaveWarning);
    storageNotice += options::Options.Language.V == "en" ? " Original backup: " : " 원본 보관: ";
    storageNotice += target ? backup : (options::Options.Language.V == "en" ? "could not create backup" : "백업 파일 생성 실패");
    SDL_Log("Settings recovery: %s; backup: %s", reason, backup.c_str());
}

void pinball_platform::RenderStorageNotice()
{
    if (storageNotice.empty()) return;
    const auto caption = std::string(translations::GetTranslation(Msg::UiSaveWarning)) + "###StorageRecovery";
    ImGui::OpenPopup(caption.c_str());
    const auto width = std::min(440.0f, std::max(1.0f, ImGui::GetIO().DisplaySize.x - 16));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    if (ImGui::BeginPopupModal(caption.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(width - 16);
        ImGui::TextUnformatted(storageNotice.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(translations::GetTranslation(Msg::GenericOk))) { storageNotice.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

const std::string& pinball_platform::RecoveryBackupPath() { return recoveryBackup; }
