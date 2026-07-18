#ifndef RME_HOTKEY_MANAGER_H
#define RME_HOTKEY_MANAGER_H

#include "main.h"
#include "main_menubar.h"

#include <optional>
#include <unordered_map>
#include <vector>
#include <wx/accel.h>

class wxListCtrl;
class wxTextCtrl;
class wxWindow;

struct HotkeyEntry {
	MenuBar::ActionID action;
	wxString defaultKey;
	std::optional<wxString> overrideKey;
	wxString description;

	wxString EffectiveKey() const {
		if (overrideKey.has_value()) {
			return *overrideKey;
		}
		return defaultKey;
	}
};

class HotkeyManager {
public:
	HotkeyManager();
	~HotkeyManager();

	void DiscoverActions(MainMenuBar* menubar);
	void LoadCustom();
	void SaveCustom();

	void SetHotkey(MenuBar::ActionID action, const wxString& key);
	wxString GetHotkey(MenuBar::ActionID action) const;
	wxString GetDefaultHotkey(MenuBar::ActionID action) const;

	void RebuildAccelerators(wxWindow* target);

	const std::unordered_map<MenuBar::ActionID, HotkeyEntry>& GetAllEntries() const;
	HotkeyEntry* FindEntry(MenuBar::ActionID action);

	void ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar);

	static wxString KeyCodeToString(int keyCode);
	static int StringToKeyCode(const wxString& keyString);
	static bool ValidateHotkeyString(const wxString& hotkey, wxString& error);

private:
	struct ActionInfo {
		wxString name;
		wxString help;
	};

	std::unordered_map<MenuBar::ActionID, HotkeyEntry> entries_;
	std::unordered_map<MenuBar::ActionID, ActionInfo> actionInfo_;

	void BuildAcceleratorEntries(std::vector<wxAcceleratorEntry>& accelEntries) const;
};

extern HotkeyManager g_hotkey_manager;

#endif
