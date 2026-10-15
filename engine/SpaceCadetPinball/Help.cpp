#include "pch.h"
#include "Help.h"
#include "translations.h"

namespace
{
	bool showHelp = false;

	void Section(Msg title, Msg body)
	{
		ImGui::Spacing();
		ImGui::TextUnformatted(translations::GetTranslation(title));
		ImGui::Separator();
		ImGui::TextWrapped("%s", translations::GetTranslation(body));
	}
}

void Help::Show()
{
	showHelp = true;
}

bool Help::IsOpen()
{
	return showHelp;
}

void Help::Render()
{
	if (!showHelp)
		return;

	const auto display = ImGui::GetIO().DisplaySize;
	const auto width = std::min(720.0f, std::max(1.0f, display.x - 12.0f));
	const auto height = std::min(680.0f, std::max(1.0f, display.y - 12.0f));
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(260.0f, width), std::min(180.0f, height)), ImVec2(width, height));
	const auto title = std::string(translations::GetTranslation(Msg::UiHelpCaption)) + "###GameHelp";
	if (ImGui::Begin(title.c_str(), &showHelp, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
			showHelp = false;

		// Reserve the close button outside the scroll area so it stays reachable.
		const auto footerHeight = ImGui::GetFrameHeightWithSpacing();
		if (ImGui::BeginChild("HelpBody", ImVec2(0, -footerHeight), false))
		{
			ImGui::TextWrapped("%s", translations::GetTranslation(Msg::HelpIntro));
			Section(Msg::HelpGoalTitle, Msg::HelpGoal);
			Section(Msg::HelpControlsTitle, Msg::HelpControls);
			ImGui::TextWrapped("%s", translations::GetTranslation(Msg::HelpShortcuts));
			Section(Msg::HelpScoringTitle, Msg::HelpScoring);
			Section(Msg::HelpTiltTitle, Msg::HelpTilt);
			Section(Msg::HelpSavingTitle, Msg::HelpSaving);
			Section(Msg::HelpCheatsTitle, Msg::HelpCheats);
		}
		ImGui::EndChild();
		if (ImGui::Button(translations::GetTranslation(Msg::UiClose), ImVec2(-1, 0)))
			showHelp = false;
	}
	ImGui::End();
}
