#include "pch.h"
#include "options.h"

#include "fullscrn.h"
#include "midi.h"
#include "pb.h"
#include "render.h"
#include "Sound.h"
#include "winmain.h"
#include "translations.h"
#include "pinball_platform.h"

constexpr int options::MaxUps, options::MaxFps, options::MinUps, options::MinFps, options::DefUps, options::DefFps;
constexpr int options::MaxSoundChannels, options::MinSoundChannels, options::DefSoundChannels;
constexpr int options::MaxVolume, options::MinVolume, options::DefVolume;

std::unordered_map<std::string, std::string> options::settings{};
bool options::ShowDialog = false;
GameInput* options::ControlWaitingForInput = nullptr;
std::vector<OptionBase*> options::AllOptions{};

optionsStruct options::Options
{
	{
		{
			"Left Flipper key",
			Msg::KEYMAPPER_FlipperL,
			{InputTypes::Keyboard, SDLK_z},
			{InputTypes::Mouse, SDL_BUTTON_LEFT},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_LEFTSHOULDER}
		},
		{
			"Right Flipper key",
			Msg::KEYMAPPER_FlipperR,
			{InputTypes::Keyboard, SDLK_SLASH},
			{InputTypes::Mouse,SDL_BUTTON_RIGHT},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}
		},
		{
			"Plunger key",
			Msg::KEYMAPPER_Plunger,
			{InputTypes::Keyboard, SDLK_SPACE},
			{InputTypes::Mouse,SDL_BUTTON_MIDDLE},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_A}
		},
		{
			"Left Table Bump key",
			Msg::KEYMAPPER_BumpLeft,
			{InputTypes::Keyboard, SDLK_x},
			{InputTypes::Mouse,SDL_BUTTON_X1},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT}
		},
		{
			"Right Table Bump key",
			Msg::KEYMAPPER_BumpRight,
			{InputTypes::Keyboard, SDLK_PERIOD},
			{InputTypes::Mouse,SDL_BUTTON_X2},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT}
		},
		{
			"Bottom Table Bump key",
			Msg::KEYMAPPER_BumpBottom,
			{InputTypes::Keyboard, SDLK_UP},
			{InputTypes::Mouse,SDL_BUTTON_X2 + 1},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_UP}
		},
		{
			"New Game",
			Msg::Menu1_New_Game,
			{InputTypes::Keyboard, SDLK_F2},
			{},
			{}
		},
		{
			"Toggle Pause",
			Msg::Menu1_Pause_Resume_Game,
			{InputTypes::Keyboard, SDLK_F3},
			{},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_START}
		},
		{
			"Toggle FullScreen",
			Msg::Menu1_Full_Screen,
			{InputTypes::Keyboard, SDLK_F4},
			{},
			{}
		},
		{
			"Toggle Sounds",
			Msg::Menu1_Sounds,
			{InputTypes::Keyboard, SDLK_F5},
			{},
			{}
		},
		{
			"Toggle Music",
			Msg::Menu1_Music,
			{InputTypes::Keyboard, SDLK_F6},
			{},
			{}
		},
		{
			"Show Control Dialog",
			Msg::Menu1_Player_Controls,
			{InputTypes::Keyboard, SDLK_F8},
			{},
			{}
		},
		{
			"Toggle Menu Display",
			Msg::Menu1_ToggleShowMenu,
			{InputTypes::Keyboard, SDLK_F9},
			{},
			{}
		},
		{
			"Exit",
			Msg::Menu1_Exit,
			{InputTypes::Keyboard, SDLK_ESCAPE},
			{},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_BACK}
		},
	},
	{"Sounds", true},
	{"Music", false},
	{"FullScreen", false},
	{"Players", 1},
	{"Screen Resolution", -1},
	{"UI Scale", 1.0f},
	{"Uniform scaling", true},
	{"Linear Filtering", true},
	{"High Quality Mode", false},
	{"Frames Per Second", DefFps},
	{"Updates Per Second", DefUps},
	{"ShowMenu", true},
	{"Uncapped Updates Per Second", false},
	{"Sound Channels", DefSoundChannels},
	{"HybridSleep", false},
	{"Prefer 3DPB Game Data", false},
	{"Integer Scaling", false},
	{"Sound Volume", DefVolume},
	{"Music Volume", DefVolume},
	{"Stereo Sound Effects", false},
	{"Debug Overlay", false},
	{"Debug Overlay Grid", true},
	{"Debug Overlay All Edges", true},
	{"Debug Overlay Ball Position", true},
	{"Debug Overlay Ball Edges", true},
	{"Debug Overlay Collision Mask", true},
	{"Debug Overlay Sprites", true},
	{"Debug Overlay Sounds", true},
	{"Debug Overlay Ball Depth Grid", true},
	{"Debug Overlay AABB", true},
	{"FontFileName", ""},
	{"Language", translations::GetCurrentLanguage()->ShortName},
	{"Hide Cursor", false},
};

float options::UIScaleForDensity(float densityDpi)
{
    // Android reports logical display density: keep the 13px font and 48px rows in dp.
    if (!std::isfinite(densityDpi) || densityDpi <= 0) return 1.0f;
    return Clamp(densityDpi / 160.0f, 0.5f, 3.0f);
}

void options::InitPrimary()
{
	settings.clear();
	auto imContext = ImGui::GetCurrentContext();
	ImGuiSettingsHandler ini_handler;
	ini_handler.TypeName = "Pinball";
	ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
	ini_handler.ReadOpenFn = MyUserData_ReadOpen;
	ini_handler.ReadLineFn = MyUserData_ReadLine;
	ini_handler.WriteAllFn = MyUserData_WriteAll;
	imContext->SettingsHandlers.push_back(ini_handler);

    bool malformedFile = false;
    if (auto file = fopenu(imContext->IO.IniFilename, "rb"))
    {
        std::string bytes;
        char chunk[4096];
        size_t count;
        while ((count = fread(chunk, 1, sizeof chunk, file))) bytes.append(chunk, count);
        fclose(file);
        malformedFile = !bytes.empty() && (bytes.find("[Pinball][Settings]") == std::string::npos || bytes.find('\0') != std::string::npos);
        if (malformedFile) pinball_platform::BackupDamagedSettings("invalid INI structure");
    }
	// Settings are loaded from disk on the first frame
	if (!imContext->SettingsLoaded)
	{
		ImGui::LoadIniSettingsFromDisk(imContext->IO.IniFilename);
		imContext->SettingsLoaded = true;
	}

	if (malformedFile) settings.clear();
    float initialUIScale = 1.0f;
#ifdef __ANDROID__
    float densityDpi = 0;
    const auto displayIndex = SDL_GetWindowDisplayIndex(winmain::MainWindow);
    if (displayIndex >= 0 && SDL_GetDisplayDPI(displayIndex, &densityDpi, nullptr, nullptr) == 0)
        initialUIScale = UIScaleForDensity(densityDpi);
#endif
	for (const auto opt : AllOptions)
    {
        // get_float inserts the default only when absent, preserving every saved scale.
        if (opt == &Options.UIScale) Options.UIScale.V = get_float(opt->Name, initialUIScale);
        else opt->Load();
    }
	PostProcessOptions();
}

void options::InitSecondary()
{
	auto maxRes = fullscrn::GetMaxResolution();
	if (Options.Resolution >= 0 && Options.Resolution > maxRes)
		Options.Resolution = maxRes;
	fullscrn::SetResolution(Options.Resolution == -1 ? maxRes : Options.Resolution);
}

void options::uninit()
{
	Options.Language.V = translations::GetCurrentLanguage()->ShortName;
	for (const auto opt : AllOptions)
	{
		if (!ShowDialog || !dynamic_cast<ControlOption*>(opt)) opt->Save();
	}
}


int options::get_int(LPCSTR lpValueName, int defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	try {
        size_t used = 0;
        auto result = std::stoi(value, &used);
        if (used != value.size()) throw std::invalid_argument("trailing settings data");
        return result;
    } catch (const std::exception&) {
        pinball_platform::BackupDamagedSettings(lpValueName);
        return defaultValue;
    }
}

void options::set_int(LPCSTR lpValueName, int data)
{
	SetSetting(lpValueName, std::to_string(data));
}

float options::get_float(LPCSTR lpValueName, float defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	try {
        size_t used = 0;
        auto result = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(result)) throw std::invalid_argument("invalid float");
        return result;
    } catch (const std::exception&) {
        pinball_platform::BackupDamagedSettings(lpValueName);
        return defaultValue;
    }
}

void options::set_float(LPCSTR lpValueName, float data)
{
	SetSetting(lpValueName, std::to_string(data));
}

void options::GetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto name = rowName + " " + std::to_string(i);
		auto inputType = static_cast<InputTypes>(get_int((name + " type").c_str(), -1));
		auto input = get_int((name + " input").c_str(), -1);
		if (inputType <= InputTypes::GameController && input != -1)
			values[i] = {inputType, input};
	}
}

void options::SetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto input = values[i];
		auto name = rowName + " " + std::to_string(i);
		set_int((name + " type").c_str(), static_cast<int>(input.Type));
		set_int((name + " input").c_str(), input.Value);
	}
}

void options::toggle(Menu1 uIDCheckItem)
{
	switch (uIDCheckItem)
	{
	case Menu1::Sounds:
		Options.Sounds ^= true;
		Sound::Enable(Options.Sounds);
		return;
	case Menu1::SoundStereo:
		Options.SoundStereo ^= true;
		return;
	case Menu1::Music:
		Options.Music ^= true;
		if (!Options.Music)
			midi::music_stop();
		else
			midi::music_play();
		return;
	case Menu1::Show_Menu:
		Options.ShowMenu ^= true;
		fullscrn::window_size_changed();
		return;
	case Menu1::Full_Screen:
		Options.FullScreen ^= true;
		fullscrn::set_screen_mode(Options.FullScreen);
		return;
	case Menu1::OnePlayer:
	case Menu1::TwoPlayers:
	case Menu1::ThreePlayers:
	case Menu1::FourPlayers:
		Options.Players = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::OnePlayer) + 1;
		break;
	case Menu1::MaximumResolution:
	case Menu1::R640x480:
	case Menu1::R800x600:
	case Menu1::R1024x768:
		{
			auto restart = false;
			int newResolution = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::R640x480);
			if (uIDCheckItem == Menu1::MaximumResolution)
			{
				restart = fullscrn::GetResolution() != fullscrn::GetMaxResolution();
				Options.Resolution = -1;
			}
			else if (newResolution <= fullscrn::GetMaxResolution())
			{
				restart = newResolution != (Options.Resolution == -1
					                            ? fullscrn::GetMaxResolution()
					                            : fullscrn::GetResolution());
				Options.Resolution = newResolution;
			}

			if (restart)
				winmain::Restart();
			break;
		}
	case Menu1::WindowUniformScale:
		Options.UniformScaling ^= true;
		fullscrn::window_size_changed();
		break;
	case Menu1::WindowLinearFilter:
		Options.LinearFiltering ^= true;
		render::recreate_screen_texture();
		break;
	case Menu1::Prefer3DPBGameData:
		Options.Prefer3DPBGameData ^= true;
		winmain::Restart();
		break;
	case Menu1::WindowIntegerScale:
		Options.IntegerScaling ^= true;
		fullscrn::window_size_changed();
		break;
	default:
		break;
	}
}

void options::InputDown(GameInput input)
{
	if (ControlWaitingForInput)
	{
		*ControlWaitingForInput = input;
		ControlWaitingForInput = nullptr;
	}
}

void options::ShowControlDialog()
{
	if (!ShowDialog)
	{
		ControlWaitingForInput = nullptr;
		ShowDialog = true;
		// Save previous controls in KVP storage.
		for (const auto& control : Options.Key)
		{
			control.Save();
		}
	}
}

void options::CancelControlDialog()
{
    for (auto& control : Options.Key) control.Load();
    ControlWaitingForInput = nullptr;
    ShowDialog = false;
}

void options::RenderControlDialog()
{
    if (!ShowDialog) return;
    const auto display = ImGui::GetIO().DisplaySize;
    const auto scale = ImGui::GetIO().FontGlobalScale;
    const auto width = std::min(720.0f * scale, std::max(1.0f, display.x - 16 * scale));
    const auto height = std::min(640.0f * scale, std::max(1.0f, display.y - 16 * scale));
    const auto caption = std::string(pb::get_rc_string(Msg::KEYMAPPER_Caption)) + "###PlayerControls";
    if (!ImGui::IsPopupOpen(caption.c_str())) ImGui::OpenPopup(caption.c_str());
    ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    if (ImGui::BeginPopupModal(caption.c_str(), &ShowDialog,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        // Only the body scrolls. Confirmation remains reachable at every viewport size.
        const auto footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("ControlsBody", ImVec2(0, -footerHeight), false))
        {
            if (ImGui::TreeNode(pb::get_rc_string(Msg::KEYMAPPER_Groupbox2)))
            {
                ImGui::TextWrapped("%s", pb::get_rc_string(Msg::KEYMAPPER_Help1));
                ImGui::TextWrapped("%s", pb::get_rc_string(Msg::KEYMAPPER_Help2));
                ImGui::TreePop();
            }
            ImGui::Spacing();
            ImGui::TextUnformatted(pb::get_rc_string(Msg::KEYMAPPER_Groupbox1));
            const bool compact = ImGui::GetContentRegionAvail().x < 520 * scale;
            const Msg bindingLabels[] = {Msg::UiBinding1, Msg::UiBinding2, Msg::UiBinding3};
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5 * scale, 10 * scale));
            if (ImGui::BeginTable("Controls", compact ? 2 : 4,
                ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn(translations::GetTranslation(Msg::UiControl));
                ImGui::TableSetupColumn(translations::GetTranslation(Msg::UiBinding1));
                if (!compact)
                {
                    ImGui::TableSetupColumn(translations::GetTranslation(Msg::UiBinding2));
                    ImGui::TableSetupColumn(translations::GetTranslation(Msg::UiBinding3));
                }
                ImGui::TableHeadersRow();
                for (auto& option : Options.Key)
                {
                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0, 0, 1));
                    if (ImGui::Button(pb::get_rc_string(option.Description), ImVec2(-1, 0)))
                        for (auto& input : option.Inputs) input = {};
                    ImGui::PopStyleColor();
                    int column = 0;
                    for (auto& input : option.Inputs)
                    {
                        if (!compact || column == 0) ImGui::TableNextColumn();
                        if (compact) ImGui::TextUnformatted(translations::GetTranslation(bindingLabels[column]));
                        ImGui::PushID(&input);
                        const auto description = ControlWaitingForInput == &input
                            ? std::string(translations::GetTranslation(Msg::UiPressKey)) : input.GetFullInputDescription();
                        if (ImGui::Button(description.c_str(), ImVec2(-1, 0))) ControlWaitingForInput = &input;
                        const bool validInput = input.Type >= InputTypes::Keyboard && input.Type <= InputTypes::GameController &&
                            input.Value >= 0 && (input.Type == InputTypes::GameController
                                ? input.Value < SDL_CONTROLLER_BUTTON_MAX : input.Value != 0);
                        const auto sharedBindings = validInput ? MapGameInput(input) : std::vector<GameBindings>{};
                        if (sharedBindings.size() > 1) {
                            std::string actions;
                            for (const auto binding : sharedBindings) {
                                if (!actions.empty()) actions += ", ";
                                actions += pb::get_rc_string(Options.Key[~binding].Description);
                            }
                            const auto keyDescription = input.GetFullInputDescription();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.25f, 1.0f));
                            ImGui::TextWrapped(translations::GetTranslation(Msg::UiControlConflict), keyDescription.c_str(), actions.c_str());
                            ImGui::TextWrapped("%s", translations::GetTranslation(Msg::UiControlConflictCancel));
                            ImGui::PopStyleColor();
                        }
                        ImGui::PopID();
                        ++column;
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        const auto buttonWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3);
        if (ImGui::Button(pb::get_rc_string(Msg::GenericOk), ImVec2(buttonWidth, 0)))
        {
            ShowDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel), ImVec2(buttonWidth, 0)))
        {
            CancelControlDialog();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(pb::get_rc_string(Msg::KEYMAPPER_Default), ImVec2(buttonWidth, 0)))
        {
            for (auto& control : Options.Key) control.Reset();
            ControlWaitingForInput = nullptr;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    if (!ShowDialog) ControlWaitingForInput = nullptr;
}

std::vector<GameBindings> options::MapGameInput(GameInput key)
{
	std::vector<GameBindings> result;
	for (auto inputId = GameBindings::Min; inputId < GameBindings::Max; inputId++)
	{
		for (auto& inputValue : Options.Key[~inputId].Inputs)
		{
			if (key == inputValue)
			{
				result.push_back(inputId);
				break;
			}
		}
	}
	return result;
}

void options::SaveIfChanged()
{
    uninit();
    static std::unordered_map<std::string, std::string> persisted;
    if (persisted != settings)
    {
        persisted = settings;
        pinball_platform::Persist();
    }
}

void options::ResetAllOptions()
{
	for (const auto opt : AllOptions)
		opt->Reset();
	PostProcessOptions();
}

void options::MyUserData_ReadLine(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line)
{
	auto& keyValueStore = *static_cast<std::unordered_map<std::string, std::string>*>(entry);
	std::string keyValue = line;
	auto separatorPos = keyValue.find('=');
	if (separatorPos != std::string::npos)
	{
		auto key = keyValue.substr(0, separatorPos);
		auto value = keyValue.substr(separatorPos + 1, keyValue.size());
		keyValueStore[key] = value;
	}
}

void* options::MyUserData_ReadOpen(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name)
{
	// There is only one custom entry
	return strcmp(name, "Settings") == 0 ? &settings : nullptr;
}

void options::MyUserData_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
	buf->appendf("[%s][%s]\n", handler->TypeName, "Settings");
	for (const auto& setting : settings)
	{
		buf->appendf("%s=%s\n", setting.first.c_str(), setting.second.c_str());
	}
	buf->append("\n");
}

void options::PostProcessOptions()
{
    if (Options.Players < 1 || Options.Players > 4 || Options.UIScale < 0.5f || Options.UIScale > 3.0f)
        pinball_platform::BackupDamagedSettings("setting range");
    Options.Players = Clamp(Options.Players.V, 1, 4);
    Options.UIScale.V = Clamp(Options.UIScale.V, 0.5f, 3.0f);
    for (auto binding = GameBindings::Min; binding < GameBindings::Max; binding++)
    {
        auto& control = Options.Key[~binding];
        for (int index = 0; index < 3; ++index)
            if (control.Inputs[index].Type < InputTypes::None || control.Inputs[index].Type > InputTypes::GameController)
            {
                pinball_platform::BackupDamagedSettings("input type");
                control.Inputs[index] = control.Defaults[index];
            }
    }
	winmain::ImIO->FontGlobalScale = Options.UIScale;
	Options.FramesPerSecond = Clamp(Options.FramesPerSecond.V, MinFps, MaxFps);
	Options.UpdatesPerSecond = Clamp(Options.UpdatesPerSecond.V, MinUps, MaxUps);
	Options.UpdatesPerSecond = std::max(Options.UpdatesPerSecond.V, Options.FramesPerSecond.V);
	Options.SoundChannels = Clamp(Options.SoundChannels.V, MinSoundChannels, MaxSoundChannels);
	Options.SoundVolume = Clamp(Options.SoundVolume.V, MinVolume, MaxVolume);
	Options.MusicVolume = Clamp(Options.MusicVolume.V, MinVolume, MaxVolume);
	translations::SetCurrentLanguage(Options.Language.V.c_str());
	winmain::UpdateFrameRate();
}

std::string GameInput::GetFullInputDescription() const
{
	std::string prefix;
	switch (Type)
	{
	case InputTypes::Keyboard:
		prefix = "Keyboard\n";
		break;
	case InputTypes::Mouse:
		prefix = "Mouse\n";
		break;
	case InputTypes::GameController:
		prefix = "Controller\n";
		break;
	case InputTypes::None:
	default:
		return "Unused";
	}

	return prefix + GetShortInputDescription();
}

std::string GameInput::GetShortInputDescription() const
{
	static LPCSTR mouseButtons[]
	{
		nullptr,
		"Left",
		"Middle",
		"Right",
		"X1",
		"X2",
	};

	static LPCSTR controllerButtons[] =
	{
		"A",
		"B",
		"X",
		"Y",
		"Back",
		"Guide",
		"Start",
		"LeftStick",
		"RightStick",
		"LeftShoulder",
		"RightShoulder",
		"DpUp",
		"DpDown",
		"DpLeft",
		"DpRight",
		"Misc1",
		"Paddle1",
		"Paddle2",
		"Paddle3",
		"Paddle4",
		"Touchpad",
	};

	std::string keyName;
	switch (Type)
	{
	case InputTypes::Keyboard:
		keyName = SDL_GetKeyName(Value);
		break;
	case InputTypes::Mouse:
		if (Value >= SDL_BUTTON_LEFT && Value <= SDL_BUTTON_X2)
			keyName = mouseButtons[Value];
		else
			keyName = "MButton" + std::to_string(Value);
		break;
	case InputTypes::GameController:
		if (Value >= SDL_CONTROLLER_BUTTON_A && Value < std::min(static_cast<int>(SDL_CONTROLLER_BUTTON_MAX), 21))
			keyName = controllerButtons[Value];
		else
			keyName = "CButton" + std::to_string(Value);
		break;
	case InputTypes::None:
	default:
		break;
	}

	return keyName;
}

const std::string& options::GetSetting(const std::string& key, const std::string& defaultValue)
{
	auto setting = settings.find(key);
	if (setting == settings.end())
	{
		settings[key] = defaultValue;
		if (ImGui::GetCurrentContext())
			ImGui::MarkIniSettingsDirty();
		return settings[key];
	}
	return setting->second;
}

void options::SetSetting(const std::string& key, const std::string& value)
{
	if (settings.count(key) && settings[key] == value) return;
	settings[key] = value;
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}

OptionBase::OptionBase(LPCSTR name): Name(name)
{
	options::AllOptions.push_back(this);
}

OptionBase::~OptionBase()
{
	auto& vec = options::AllOptions;
	auto position = std::find(vec.begin(), vec.end(), this);
	if (position != vec.end())
		vec.erase(position);
}

std::string ControlOption::GetShortcutDescription() const
{
	std::string result;
	for (const auto& input : Inputs)
	{
		if (input.Type != InputTypes::None)
		{
			result = input.GetShortInputDescription();
			break;
		}
	}
	return result;
}
