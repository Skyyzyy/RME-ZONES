#ifndef RME_HOTKEY_DIALOG_H
#define RME_HOTKEY_DIALOG_H

#include "main.h"
#include "main_menubar.h"
#include "hotkey_manager.h"

#include <set>
#include <unordered_map>
#include <vector>
#include <wx/dialog.h>

class wxListCtrl;
class wxTextCtrl;

class HotkeyDialog : public wxDialog {
public:
	HotkeyDialog(wxWindow* parent, MainMenuBar* menubar,
		std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries,
		const std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo);

private:
	void BuildDisplayItems();
	void PopulateList();

	struct DisplayItem {
		wxString category;
		MenuBar::ActionID action;
		wxString actionName;
		wxString hotkey;
	};

	MainMenuBar* menubar_;
	std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries_;
	const std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo_;
	std::vector<DisplayItem> displayItems_;
	wxListCtrl* hotkeyList_ = nullptr;
	wxTextCtrl* hotkeyEdit_ = nullptr;
	std::set<int> currentModifiers_;
};

#endif
