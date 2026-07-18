#ifndef RME_HOTKEY_DIALOG_H
#define RME_HOTKEY_DIALOG_H

#include "main.h"
#include "main_menubar.h"
#include "hotkey_manager.h"

#include <set>
#include <unordered_map>
#include <vector>
#include <wx/dialog.h>
#include <wx/event.h>

class wxListCtrl;
class wxTextCtrl;

class HotkeyDialog : public wxDialog {
public:
	HotkeyDialog(wxWindow* parent, MainMenuBar* menubar,
		std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries,
		std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo);

private:
	void BuildDisplayItems();
	void BuildMenuHelpEntries();
	void PopulateList();
	void OnKeyDown(wxKeyEvent& event);
	void OnSetButton(wxCommandEvent& event);
	void OnHotkeySearch(wxCommandEvent& event);
	void OnHelpSearch(wxCommandEvent& event);

	static bool ContainsIgnoreCase(const wxString& source, const wxString& search);

	struct DisplayItem {
		wxString category;
		MenuBar::ActionID action;
		wxString actionName;
		wxString hotkey;
		wxString help;
	};

	struct MenuHelpEntry {
		wxString menu;
		wxString text;
		wxString action;
		wxString help;
		wxString shortcut;
	};

	MainMenuBar* menubar_;
	std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries_;
	std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo_;
	std::vector<DisplayItem> displayItems_;
	std::vector<MenuHelpEntry> menuHelpEntries_;
	wxListCtrl* hotkeyList_ = nullptr;
	wxTextCtrl* hotkeyEdit_ = nullptr;
	wxTextCtrl* hotkeySearch_ = nullptr;
	wxTextCtrl* helpSearch_ = nullptr;
	std::set<int> currentModifiers_;
};

#endif
