#pragma once
#include <stdint.h>

#ifdef __cplusplus
#include <string>
#include <SDL_rect.h>
union SDL_Event;
extern "C" {
#endif
/* Called from host threads. Commands are consumed on the SDL game thread.
   Session ids are monotonic: pause/reset rejects older lifecycle commands. */
void platform_notify_pause(int paused, uint32_t session_id);
void platform_reset_input_session(uint32_t session_id);
void platform_cancel_inputs(void);
void platform_flush_settings(void);
void platform_activate_audio(void);
int platform_is_english(void);
#ifdef __EMSCRIPTEN__
int platform_web_name_edit(int session_id, const char* value, int cursor, int anchor);
void platform_web_name_submit(int session_id);
#endif
#ifdef __ANDROID__
void platform_android_ready(void);
#endif
#ifdef __cplusplus
}

class pinball_platform
{
public:
    struct ControlLayout
    {
        SDL_Rect Buttons[7];
        SDL_Rect BoardBounds;
        int HeaderHeight, DockHeight;
        float TouchScale;
        bool Mobile, Landscape;
    };
    static ControlLayout CalculateLayout(int width, int height, float touchScale, bool mobile, int menuHeight);
    static ControlLayout GetControlLayout();
    static int HitControl(float x, float y);
    static void Initialize();
    static void InitializeTextInput();
    static void RefreshTextInput();
    static void SuspendTextInput();
#ifdef __EMSCRIPTEN__
    static void BeginWebNameFrame();
    static void PrepareWebName(char* value, int size);
    static int WebNameCallback(struct ImGuiInputTextCallbackData* data);
    static bool PublishWebName(const char* value, const char* label);
    static void EndWebNameFrame(bool popupOpen);
#endif
    static bool ProcessEvent(const SDL_Event& event);
    static void CancelInputs();
    static void AfterSimulation();
    static int ControlsHeight();
    static void RenderControls();
    static void Persist();
    static void BackupDamagedSettings(const char* reason);
    static void RenderStorageNotice();
    static const std::string& RecoveryBackupPath();
};
#endif
