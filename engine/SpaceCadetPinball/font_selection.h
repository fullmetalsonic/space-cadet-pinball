#pragma once

class font_selection
{
public:
	static void ShowDialog();
	static void RenderDialog();
	// Call after loading options, before initializing the ImGui renderer.
	static bool LoadFonts();
private:
	static bool ShowDialogFlag;
	static char DialogInputBuffer[512];
};
