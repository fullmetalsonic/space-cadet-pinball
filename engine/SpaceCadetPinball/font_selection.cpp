#include "pch.h"
#include "font_selection.h"

#include "options.h"
#include "pb.h"
#include "score.h"
#include "winmain.h"
#include "translations.h"
#include "EmbeddedData.h"

namespace
{
	// ImFontConfig retains this pointer until the atlas is built/rebuilt.
	ImVector<ImWchar> fontRanges;
	const char* popupId = "###FontSelection";

	ImFont* LoadFile(const std::string& path, bool merge)
	{
		auto file = fopenu(path.c_str(), "rb");
		if (!file)
			return nullptr;
		// ImGui's own file opener uses narrow fopen under MinGW/Clang.
		// Read through the game's UTF-8 adapter once, then transfer the buffer
		// to the atlas so a Korean installation path does not get reopened.
		if (fseek(file, 0, SEEK_END) != 0)
		{
			fclose(file);
			return nullptr;
		}
		const auto size = ftell(file);
		if (size <= 0 || size > 0x7FFFFFFF || fseek(file, 0, SEEK_SET) != 0)
		{
			fclose(file);
			return nullptr;
		}
		auto data = IM_ALLOC(static_cast<size_t>(size));
		if (!data)
		{
			fclose(file);
			return nullptr;
		}
		const auto read = fread(data, 1, static_cast<size_t>(size), file);
		fclose(file);
		if (read != static_cast<size_t>(size))
		{
			IM_FREE(data);
			return nullptr;
		}
		ImFontConfig config{};
		config.OversampleV = 2;
		config.OversampleH = 2;
		config.MergeMode = merge;
		config.FontDataOwnedByAtlas = true;
		return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(size), 13.0f, &config, fontRanges.Data);
	}
}
bool font_selection::ShowDialogFlag = false;
char font_selection::DialogInputBuffer[512];

bool font_selection::LoadFonts()
{
	translations::GetGlyphRange(&fontRanges);
	const auto& requested = options::Options.FontFileName.V;
	auto customFont = requested.empty() ? nullptr : LoadFile(requested, false);
	if (!requested.empty() && !customFont)
		printf("Failed to load selected font; using the bundled font.\n");

	// The custom font keeps its existing role; merge bundled glyphs it lacks.
	char* basePath = SDL_GetBasePath();
	const std::string base = basePath ? basePath : "";
	SDL_free(basePath);
	const std::string candidates[] = {base + EmbeddedData::BundledFontPath, EmbeddedData::BundledFontPath};
	ImFont* bundledFont = nullptr;
	for (const auto& path : candidates)
	{
		bundledFont = LoadFile(path, customFont != nullptr);
		if (bundledFont)
			break;
	}
	if (!bundledFont)
		printf("Bundled Korean font missing: assets/fonts/NotoSansKR.ttf\n");
	if (!customFont && !bundledFont)
		ImGui::GetIO().Fonts->AddFontDefault();
	return ImGui::GetIO().Fonts->Build() && bundledFont != nullptr;
}

void font_selection::ShowDialog()
{
	ShowDialogFlag = true;
}

void font_selection::RenderDialog()
{
	const auto popupName = std::string(translations::GetTranslation(Msg::UiFontCaption)) + popupId;
	if (ShowDialogFlag == true)
	{
		snprintf(DialogInputBuffer, sizeof(DialogInputBuffer), "%s", options::Options.FontFileName.V.c_str());
		ShowDialogFlag = false;
		if (!ImGui::IsPopupOpen(popupName.c_str()))
		{
			ImGui::OpenPopup(popupName.c_str());
		}
	}

	bool unused_open = true;
	const auto displayWidth = ImGui::GetIO().DisplaySize.x;
	ImGui::SetNextWindowSize(ImVec2(std::min(480.0f, std::max(1.0f, displayWidth - 12.0f)), 0));
	if (ImGui::BeginPopupModal(popupName.c_str(), &unused_open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(translations::GetTranslation(Msg::UiFontFile));
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##Font", DialogInputBuffer, IM_ARRAYSIZE(DialogInputBuffer));
		ImGui::TextWrapped("%s", translations::GetTranslation(Msg::UiFontInfo));

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk)))
		{
			options::Options.FontFileName.V = DialogInputBuffer;
			ImGui::CloseCurrentPopup();
			winmain::Restart();
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}
