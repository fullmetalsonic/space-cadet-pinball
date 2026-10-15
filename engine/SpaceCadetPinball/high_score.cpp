#include "pch.h"
#include "high_score.h"

#include "options.h"
#include "pb.h"
#include "score.h"
#include "translations.h"
#include "pinball_platform.h"

bool high_score::dlg_enter_name;
bool high_score::ShowDialog = false;
high_score_entry high_score::DlgData;
std::vector<high_score_entry> high_score::ScoreQueue;
high_score_struct high_score::highscore_table[5];

int high_score::read()
{
	char Buffer[20];

	uint32_t checkSum = 0;
	clear_table();
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = highscore_table[position];

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Name");
		auto name = options::GetSetting(Buffer, "");
		strncpy(tablePtr.Name, name.c_str(), sizeof tablePtr.Name - 1);
		tablePtr.Name[sizeof tablePtr.Name - 1] = 0;
		if (name.size() >= sizeof tablePtr.Name) pinball_platform::BackupDamagedSettings("high score name length");

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Score");
		tablePtr.Score = options::get_int(Buffer, tablePtr.Score);

		for (int i = static_cast<int>(strlen(tablePtr.Name)); --i >= 0; checkSum += tablePtr.Name[i])
		{
		}
		checkSum += tablePtr.Score;
	}

	auto verificationPresent = !options::GetSetting("Verification", "").empty();
    auto verification = verificationPresent ? static_cast<uint32_t>(options::get_int("Verification", 7)) : checkSum;
    if (checkSum != verification)
    {
        pinball_platform::BackupDamagedSettings("high score checksum");
        clear_table();
    }
	return 0;
}

int high_score::write()
{
	char Buffer[20];

	uint32_t checkSum = 0;
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = highscore_table[position];

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Name");
		options::SetSetting(Buffer, tablePtr.Name);

		snprintf(Buffer, sizeof Buffer, "%d", position);
		strcat(Buffer, ".Score");
		options::set_int(Buffer, tablePtr.Score);

		for (int i = static_cast<int>(strlen(tablePtr.Name)); --i >= 0; checkSum += tablePtr.Name[i])
		{
		}
		checkSum += tablePtr.Score;
	}

	options::set_int("Verification", static_cast<int32_t>(checkSum));
	pinball_platform::Persist();
	return 0;
}

void high_score::clear_table()
{
	for (auto& table : highscore_table)
	{
		table.Score = -999;
		table.Name[0] = 0;
	}
}

int high_score::get_score_position(int score)
{
	if (score <= 0)
		return -1;

	for (int position = 0; position < 5; position++)
	{
		if (highscore_table[position].Score < score)
			return position;
	}
	return -1;
}

void high_score::place_new_score_into(high_score_entry data)
{
	if (data.Position >= 0 && data.Position < 5)
	{
		for (int i = 4; i > data.Position; i--)
		{
			highscore_table[i] = highscore_table[i - 1];
		}

		data.Entry.Name[31] = 0;
		highscore_table[data.Position] = data.Entry;
		write();
	}
}

void high_score::show_high_score_dialog()
{
	ShowDialog = true;
}

void high_score::show_and_set_high_score_dialog(high_score_entry score)
{
	ScoreQueue.insert(ScoreQueue.begin(), score);
	ShowDialog = true;
}

void high_score::RenderHighScoreDialog()
{
#ifdef __EMSCRIPTEN__
    pinball_platform::BeginWebNameFrame();
    bool webNameBlocked = false;
#endif
	if (ShowDialog == true)
	{
		ShowDialog = false;
		if (!ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption)))
		{
			dlg_enter_name = false;
			while (!ScoreQueue.empty())
			{
				DlgData = ScoreQueue.back();
				ScoreQueue.pop_back();
				if (DlgData.Position < 0 || DlgData.Position > 4)
				{
					DlgData.Position = get_score_position(DlgData.Entry.Score);
				}

				if (DlgData.Position != -1)
				{
					dlg_enter_name = true;
					break;
				}
			}

			ImGui::OpenPopup(pb::get_rc_string(Msg::HIGHSCORES_Caption));
		}
	}

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // Keep the complete score dialog visible when a mobile keyboard shrinks the viewport.
    if (pinball_platform::GetControlLayout().Mobile) {
        auto window = ImGui::FindWindowByName(pb::get_rc_string(Msg::HIGHSCORES_Caption));
        if (window && ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption))) {
            auto viewport = ImGui::GetMainViewport();
            const float margin = 8.0f;
            const auto minimum = ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin);
            const auto maximum = ImVec2(std::max(minimum.x, viewport->WorkPos.x + viewport->WorkSize.x - window->Size.x - margin),
                std::max(minimum.y, viewport->WorkPos.y + viewport->WorkSize.y - window->Size.y - margin));
            const auto position = ImVec2(std::max(minimum.x, std::min(window->Pos.x, maximum.x)),
                std::max(minimum.y, std::min(window->Pos.y, maximum.y)));
            if (position.x != window->Pos.x || position.y != window->Pos.y)
                ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        }
    }
#endif
	bool unused_open = true, textBoxSubmit = false;
	if (ImGui::BeginPopupModal(pb::get_rc_string(Msg::HIGHSCORES_Caption), &unused_open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::BeginTable("table1", 3, ImGuiTableFlags_Borders))
		{
			char buf[36];
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Rank));
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Name));
			ImGui::TableSetupColumn(pb::get_rc_string(Msg::HIGHSCORES_Score));
			ImGui::TableHeadersRow();

			for (int offset = 0, row = 0; row < 5; row++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				snprintf(buf, sizeof buf, "%d", row + 1);
				ImGui::TextUnformatted(buf);

				auto currentRow = &highscore_table[row + offset];
				auto score = currentRow->Score;
				ImGui::TableNextColumn();
				if (dlg_enter_name && DlgData.Position == row)
				{
					offset = -1;
					score = DlgData.Entry.Score;
					ImGui::PushItemWidth(200);

					if (ImGui::IsWindowAppearing())
					{
						ImGui::SetKeyboardFocusHere(0);
					}
#ifdef __EMSCRIPTEN__
                    pinball_platform::PrepareWebName(DlgData.Entry.Name, IM_ARRAYSIZE(DlgData.Entry.Name));
                    const auto nameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackAlways;
                    const auto nameSubmitted = ImGui::InputText("", DlgData.Entry.Name, IM_ARRAYSIZE(DlgData.Entry.Name), nameFlags, pinball_platform::WebNameCallback);
                    const auto browserSubmitted = pinball_platform::PublishWebName(DlgData.Entry.Name, pb::get_rc_string(Msg::HIGHSCORES_Name));
                    if (nameSubmitted || browserSubmitted)
#else
					if (ImGui::InputText("", DlgData.Entry.Name, IM_ARRAYSIZE(DlgData.Entry.Name),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
#endif
					{
						textBoxSubmit = true;
					}
				}
				else
				{
					ImGui::TextUnformatted(currentRow->Name);
				}

				ImGui::TableNextColumn();
				score::string_format(score, buf);
				ImGui::TextUnformatted(buf);
			}
			ImGui::EndTable();
		}

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk)) || textBoxSubmit)
		{
			if (dlg_enter_name)
			{
				place_new_score_into(DlgData);
			}
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel)))
			ImGui::CloseCurrentPopup();

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::HIGHSCORES_Clear)))
			ImGui::OpenPopup("Confirm");
		if (ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_MenuBar))
		{
			ImGui::TextUnformatted(pb::get_rc_string(Msg::STRING141));
			if (ImGui::Button(pb::get_rc_string(Msg::GenericOk), ImVec2(120, 0)))
			{
				clear_table();
				write();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel), ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

#ifdef __EMSCRIPTEN__
        // Confirm is scoped to this score window; querying after EndPopup uses
        // the parent's ID stack and cannot identify the nested popup.
        webNameBlocked = ImGui::IsPopupOpen("Confirm");
#endif
		ImGui::EndPopup();

		// Reenter dialog for the next score in the queue
		if (!ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption)) && !ScoreQueue.empty())
		{
			ShowDialog = true;
		}
	}
#ifdef __EMSCRIPTEN__
    pinball_platform::EndWebNameFrame(ImGui::IsPopupOpen(pb::get_rc_string(Msg::HIGHSCORES_Caption)) && !webNameBlocked);
#endif
}
