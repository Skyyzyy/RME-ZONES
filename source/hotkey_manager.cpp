#include "main.h"
#include "hotkey_manager.h"
#include "application.h"
#include "settings.h"
#include "gui.h"
#include "gui_ids.h"

#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/event.h>

#include <algorithm>
#include <functional>
#include <set>

HotkeyManager g_hotkey_manager;

HotkeyManager::HotkeyManager() = default;
HotkeyManager::~HotkeyManager() = default;

void HotkeyManager::DiscoverActions(MainMenuBar* menubar) {
	if (!menubar) {
		return;
	}

	actionInfo_.clear();
	entries_.clear();

	// Load default hotkeys from menubar.xml
	wxString path = g_gui.GetDataDirectory() + "menubar.xml";
	pugi::xml_document doc;
	if (!doc.load_file(path.mb_str())) {
		return;
	}

	pugi::xml_node menubarNode = doc.child("menubar");
	if (!menubarNode) {
		return;
	}

	// First pass: collect all actions from menubar.xml with their default hotkeys
	std::unordered_map<std::string, std::pair<wxString, wxString>> xmlActions;

	std::function<void(pugi::xml_node)> collectActions = [&](pugi::xml_node node) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string actionStr = item.attribute("action").as_string();
			if (!actionStr.empty()) {
				std::string hotkey = item.attribute("hotkey").as_string();
				std::string help = item.attribute("help").as_string();
				xmlActions[actionStr] = {wxString(hotkey), wxString(help)};
			}
		}
		for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
			collectActions(menu);
		}
	};
	collectActions(menubarNode);

	// Second pass: match XML actions with MainMenuBar ActionID enum
	const auto& actions = menubar->GetActions();
	for (const auto& pair : actions) {
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(pair.second->id);
		const std::string& actionName = pair.first;

		wxString defaultKey;
		wxString description;

		auto xmlIt = xmlActions.find(actionName);
		if (xmlIt != xmlActions.end()) {
			defaultKey = xmlIt->second.first;
			description = xmlIt->second.second;
		}

		ActionInfo info;
		info.name = wxString(actionName);
		info.help = description;
		actionInfo_[actionId] = info;

		HotkeyEntry entry;
		entry.action = actionId;
		entry.defaultKey = defaultKey;
		entry.description = description;
		entries_[actionId] = entry;
	}
}

void HotkeyManager::LoadCustom() {
	wxConfigBase* config = &Settings::getConfigObject();
	config->SetPath("/Hotkeys/");

	long dummy;
	wxString actionName;
	bool hasEntry = config->GetFirstEntry(actionName, dummy);
	while (hasEntry) {
		wxString savedKey = config->Read(actionName, "");
		// Skip the numerical hotkeys entry
		if (actionName != "NUMERICAL_HOTKEYS") {
			// Match by name
			for (auto& pair : entries_) {
				auto infoIt = actionInfo_.find(pair.first);
				if (infoIt != actionInfo_.end() && infoIt->second.name == actionName) {
					pair.second.overrideKey = savedKey;
					break;
				}
			}
		}
		hasEntry = config->GetNextEntry(actionName, dummy);
	}

	config->SetPath("/");
}

void HotkeyManager::SaveCustom() {
	wxConfigBase* config = &Settings::getConfigObject();
	config->SetPath("/Hotkeys/");

	// Collect existing entries (like NUMERICAL_HOTKEYS) to preserve them
	std::map<wxString, wxString> preserved;
	long dummy;
	wxString entryName;
	bool hasEntry = config->GetFirstEntry(entryName, dummy);
	while (hasEntry) {
		if (entryName != "NUMERICAL_HOTKEYS") {
			// Delete old hotkey entries we manage
			config->DeleteEntry(entryName, false);
		}
		hasEntry = config->GetNextEntry(entryName, dummy);
	}

	// Write our managed hotkeys
	for (const auto& pair : entries_) {
		const HotkeyEntry& entry = pair.second;
		auto infoIt = actionInfo_.find(pair.first);
		if (infoIt != actionInfo_.end()) {
			if (entry.overrideKey.has_value()) {
				config->Write(infoIt->second.name, *entry.overrideKey);
			}
		}
	}

	config->SetPath("/");
	config->Flush();
}

void HotkeyManager::SetHotkey(MenuBar::ActionID action, const wxString& key) {
	auto it = entries_.find(action);
	if (it != entries_.end()) {
		if (key.empty()) {
			it->second.overrideKey.reset();
		} else {
			it->second.overrideKey = key;
		}
	}
}

wxString HotkeyManager::GetHotkey(MenuBar::ActionID action) const {
	auto it = entries_.find(action);
	if (it != entries_.end()) {
		return it->second.EffectiveKey();
	}
	return "";
}

wxString HotkeyManager::GetDefaultHotkey(MenuBar::ActionID action) const {
	auto it = entries_.find(action);
	if (it != entries_.end()) {
		return it->second.defaultKey;
	}
	return "";
}

void HotkeyManager::BuildAcceleratorEntries(std::vector<wxAcceleratorEntry>& accelEntries) const {
	for (const auto& pair : entries_) {
		const HotkeyEntry& entry = pair.second;
		wxString keyStr = entry.EffectiveKey();
		if (keyStr.empty()) {
			continue;
		}

		wxAcceleratorEntry* accel = wxAcceleratorEntry::Create(wxString("\t") + keyStr);
		if (accel) {
			int eventId = MAIN_FRAME_MENU + static_cast<int>(entry.action);
			accel->Set(accel->GetFlags(), accel->GetKeyCode(), eventId);
			accelEntries.push_back(*accel);
			delete accel;
		}
	}
}

void HotkeyManager::RebuildAccelerators(wxWindow* target) {
	if (!target) {
		return;
	}

	std::vector<wxAcceleratorEntry> accelEntries;
	BuildAcceleratorEntries(accelEntries);

	if (!accelEntries.empty()) {
		wxAcceleratorTable table(static_cast<int>(accelEntries.size()), accelEntries.data());
		target->SetAcceleratorTable(table);
	}
}

const std::unordered_map<MenuBar::ActionID, HotkeyEntry>& HotkeyManager::GetAllEntries() const {
	return entries_;
}

HotkeyEntry* HotkeyManager::FindEntry(MenuBar::ActionID action) {
	auto it = entries_.find(action);
	if (it != entries_.end()) {
		return &it->second;
	}
	return nullptr;
}

wxString HotkeyManager::KeyCodeToString(int keyCode) {
	return wxAcceleratorEntry(wxACCEL_NORMAL, keyCode, 0).ToString();
}

int HotkeyManager::StringToKeyCode(const wxString& keyString) {
	wxAcceleratorEntry entry;
	entry.FromString(keyString);
	return entry.GetKeyCode();
}

bool HotkeyManager::ValidateHotkeyString(const wxString& hotkey, wxString& error) {
	if (hotkey.empty()) {
		return true;
	}

	auto IsValidModifier = [](const wxString& mod) -> bool {
		return mod == "Ctrl" || mod == "Alt" || mod == "Shift";
	};

	auto IsValidKey = [](const wxString& key) -> bool {
		if (key.length() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
			return true;
		}
		if (key.StartsWith("F") && key.length() <= 3) {
			long num;
			wxString numStr = key.Mid(1);
			if (numStr.ToLong(&num)) {
				return num >= 1 && num <= 12;
			}
		}
		static const wxString validSpecialKeys[] = {
			"Space", "Tab", "Enter", "Esc",
			"Left", "Right", "Up", "Down",
			"Home", "End", "PgUp", "PgDn",
			"Insert", "Delete", "Plus", "Minus"
		};
		for (const auto& sk : validSpecialKeys) {
			if (key == sk) return true;
		}
		return false;
	};

	wxArrayString parts = wxSplit(hotkey, '+');
	if (parts.IsEmpty() || !IsValidKey(parts.Last())) {
		error = "Invalid key. Must be A-Z, F1-F12, or a special key";
		return false;
	}
	for (size_t i = 0; i < parts.size() - 1; ++i) {
		if (!IsValidModifier(parts[i].Trim())) {
			error = "Invalid modifier. Must be Ctrl, Alt, or Shift";
			return false;
		}
	}
	return true;
}

void HotkeyManager::ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar) {
	wxDialog* dialog = new wxDialog(parent, wxID_ANY, "Hotkey Configuration",
		wxDefaultPosition, wxSize(700, 500));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxListCtrl* hotkeyList = new wxListCtrl(dialog, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	hotkeyList->InsertColumn(0, "Menu", wxLIST_FORMAT_LEFT, 180);
	hotkeyList->InsertColumn(1, "Action", wxLIST_FORMAT_LEFT, 220);
	hotkeyList->InsertColumn(2, "Hotkey", wxLIST_FORMAT_LEFT, 150);

	// Build category map from menubar.xml
	wxString path = g_gui.GetDataDirectory() + "menubar.xml";
	pugi::xml_document doc;
	std::unordered_map<std::string, wxString> actionCategory;

	if (doc.load_file(path.mb_str())) {
		std::function<void(pugi::xml_node, wxString)> collectCategories = [&](pugi::xml_node node, wxString currentMenu) {
			for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
				std::string action = item.attribute("action").as_string();
				if (!action.empty()) {
					actionCategory[action] = currentMenu;
				}
			}
			for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
				wxString menuName = wxString(menu.attribute("name").as_string());
				wxString fullPath = currentMenu.empty() ? menuName : currentMenu + " > " + menuName;
				collectCategories(menu, fullPath);
				// Also register items directly in this submenu with the parent category
				for (pugi::xml_node item = menu.child("item"); item; item = item.next_sibling("item")) {
					std::string action = item.attribute("action").as_string();
					if (!action.empty() && actionCategory.find(action) == actionCategory.end()) {
						actionCategory[action] = currentMenu;
					}
				}
			}
		};
		collectCategories(doc.child("menubar"), "");
	}

	// Populate the list
	struct DisplayItem {
		wxString category;
		MenuBar::ActionID action;
		wxString actionName;
		wxString hotkey;
	};
	std::vector<DisplayItem> displayItems;

	// Get the ordered action list from menubar
	const auto& actions = menubar->GetActions();
	for (const auto& pair : actions) {
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(pair.second->id);
		auto entryIt = entries_.find(actionId);
		if (entryIt == entries_.end()) {
			continue;
		}

		wxString category;
		auto catIt = actionCategory.find(pair.first);
		if (catIt != actionCategory.end()) {
			category = catIt->second;
		}

		displayItems.push_back({
			category,
			actionId,
			wxString(pair.first),
			entryIt->second.EffectiveKey()
		});
	}

	// Sort by category then action name
	std::sort(displayItems.begin(), displayItems.end(),
		[](const DisplayItem& a, const DisplayItem& b) {
			if (a.category != b.category) return a.category < b.category;
			return a.actionName < b.actionName;
		});

	for (size_t i = 0; i < displayItems.size(); ++i) {
		long idx = hotkeyList->InsertItem(static_cast<long>(i), displayItems[i].category);
		hotkeyList->SetItem(idx, 1, displayItems[i].actionName);
		hotkeyList->SetItemPtrData(idx, static_cast<long>(static_cast<int>(displayItems[i].action)));
		wxString hk = displayItems[i].hotkey;
		if (hk.empty()) {
			hk = "(none)";
		}
		hotkeyList->SetItem(idx, 2, hk);
	}

	// Edit area
	wxBoxSizer* editSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* label = new wxStaticText(dialog, wxID_ANY, "Hotkey:");
	wxTextCtrl* hotkeyEdit = new wxTextCtrl(dialog, wxID_ANY, "",
		wxDefaultPosition, wxSize(150, -1), wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	hotkeyEdit->SetEditable(false);

	std::set<int> currentModifiers;

	hotkeyEdit->Bind(wxEVT_KEY_DOWN, [&currentModifiers, hotkeyEdit](wxKeyEvent& event) {
		int keyCode = event.GetKeyCode();

		if (keyCode == WXK_ESCAPE) {
			event.Skip();
			return;
		}

		if (keyCode == WXK_SHIFT || keyCode == WXK_CONTROL || keyCode == WXK_ALT) {
			currentModifiers.insert(keyCode);
			wxString currentValue = hotkeyEdit->GetValue();
			wxString modStr;
			if (keyCode == WXK_SHIFT) modStr = "Shift+";
			else if (keyCode == WXK_CONTROL) modStr = "Ctrl+";
			else if (keyCode == WXK_ALT) modStr = "Alt+";

			if (!currentValue.Contains(modStr)) {
				if (!currentValue.empty() && !currentValue.EndsWith("+")) {
					currentValue += "+";
				}
				currentValue += modStr;
				hotkeyEdit->SetValue(currentValue);
			}
			event.Skip(false);
			return;
		}

		if ((keyCode >= 'A' && keyCode <= 'Z') ||
			(keyCode >= '0' && keyCode <= '9') ||
			(keyCode >= WXK_F1 && keyCode <= WXK_F12) ||
			keyCode == WXK_SPACE || keyCode == WXK_TAB || keyCode == WXK_RETURN ||
			keyCode == WXK_LEFT || keyCode == WXK_RIGHT ||
			keyCode == WXK_UP || keyCode == WXK_DOWN ||
			keyCode == WXK_HOME || keyCode == WXK_END ||
			keyCode == WXK_PAGEUP || keyCode == WXK_PAGEDOWN ||
			keyCode == WXK_INSERT || keyCode == WXK_DELETE) {

			if (keyCode >= 'a' && keyCode <= 'z') {
				keyCode = keyCode - 'a' + 'A';
			}

			wxString finalKey;
			if (keyCode >= WXK_F1 && keyCode <= WXK_F12) {
				finalKey = wxString::Format("F%d", keyCode - WXK_F1 + 1);
			} else if (keyCode == WXK_SPACE) finalKey = "Space";
			else if (keyCode == WXK_TAB) finalKey = "Tab";
			else if (keyCode == WXK_RETURN) finalKey = "Enter";
			else if (keyCode == WXK_ESCAPE) finalKey = "Esc";
			else if (keyCode == WXK_LEFT) finalKey = "Left";
			else if (keyCode == WXK_RIGHT) finalKey = "Right";
			else if (keyCode == WXK_UP) finalKey = "Up";
			else if (keyCode == WXK_DOWN) finalKey = "Down";
			else if (keyCode == WXK_HOME) finalKey = "Home";
			else if (keyCode == WXK_END) finalKey = "End";
			else if (keyCode == WXK_PAGEUP) finalKey = "PgUp";
			else if (keyCode == WXK_PAGEDOWN) finalKey = "PgDn";
			else if (keyCode == WXK_INSERT) finalKey = "Insert";
			else if (keyCode == WXK_DELETE) finalKey = "Delete";
			else {
				finalKey = wxString(static_cast<wxChar>(keyCode));
			}

			wxString currentValue = hotkeyEdit->GetValue();
			if (!currentValue.empty() && !currentValue.EndsWith("+")) {
				size_t lastPlus = currentValue.find_last_of('+');
				if (lastPlus != wxString::npos) {
					currentValue = currentValue.substr(0, lastPlus + 1);
				} else {
					currentValue = "";
				}
			}
			currentValue += finalKey;
			hotkeyEdit->SetValue(currentValue);
			event.Skip(false);
			return;
		}

		if (keyCode != WXK_BACK) {
			event.Skip(false);
		}
	});

	hotkeyEdit->Bind(wxEVT_KEY_UP, [&currentModifiers, hotkeyEdit](wxKeyEvent& event) {
		int keyCode = event.GetKeyCode();
		if (keyCode == WXK_BACK) {
			hotkeyEdit->SetValue("");
			currentModifiers.clear();
		}
		event.Skip();
	});

	wxButton* setButton = new wxButton(dialog, wxID_ANY, "Set");
	editSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	editSizer->Add(hotkeyEdit, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	editSizer->Add(setButton, 0);

	// Handle list selection
	hotkeyList->Bind(wxEVT_LIST_ITEM_SELECTED, [hotkeyList, hotkeyEdit](wxListEvent& event) {
		wxListItem item;
		item.SetId(event.GetIndex());
		item.SetColumn(2);
		item.SetMask(wxLIST_MASK_TEXT);
		hotkeyList->GetItem(item);
		wxString hk = item.GetText();
		if (hk == "(none)") hk = "";
		hotkeyEdit->SetValue(hk);
	});

	// Handle set button
	setButton->Bind(wxEVT_BUTTON, [this, dialog, hotkeyList, hotkeyEdit](wxCommandEvent&) {
		wxString newHotkey = hotkeyEdit->GetValue();
		wxString error;

		long selectedIndex = hotkeyList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (selectedIndex == -1) {
			wxMessageBox("Please select an action first", "Error", wxOK | wxICON_ERROR);
			return;
		}

		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(hotkeyList->GetItemData(selectedIndex));

		if (!ValidateHotkeyString(newHotkey, error)) {
			wxMessageBox(error, "Invalid Hotkey", wxOK | wxICON_ERROR);
			return;
		}

		// Check for duplicates
		bool reassign = true;
		for (const auto& pair : entries_) {
			if (pair.first != actionId && !pair.second.EffectiveKey().empty() &&
				pair.second.EffectiveKey() == newHotkey && !newHotkey.empty()) {
				auto infoIt = actionInfo_.find(pair.first);
				wxString conflictName = infoIt != actionInfo_.end() ? infoIt->second.name : wxString("Unknown");

				wxMessageDialog* confirmDialog = new wxMessageDialog(
					dialog,
					"This hotkey is already assigned to: " + conflictName +
					"\n\nDo you want to reassign it?",
					"Duplicate Hotkey",
					wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
				);

				if (confirmDialog->ShowModal() == wxID_YES) {
					entries_[pair.first].overrideKey = "";
				} else {
					reassign = false;
				}
				delete confirmDialog;
				break;
			}
		}

		if (!reassign) {
			return;
		}

		if (newHotkey.empty()) {
			entries_[actionId].overrideKey.reset();
		} else {
			entries_[actionId].overrideKey = newHotkey;
		}

		wxListItem item;
		item.SetId(selectedIndex);
		item.SetColumn(2);
		item.SetMask(wxLIST_MASK_TEXT);
		hotkeyList->GetItem(item);
		wxString displayHk = newHotkey.empty() ? wxString("(none)") : newHotkey;
		hotkeyList->SetItem(selectedIndex, 2, displayHk);
	});

	mainSizer->Add(hotkeyList, 1, wxEXPAND | wxALL, 5);
	mainSizer->Add(editSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* saveButton = new wxButton(dialog, wxID_OK, "Save");
	wxButton* cancelButton = new wxButton(dialog, wxID_CANCEL, "Cancel");
	buttonSizer->Add(saveButton, 0, wxRIGHT, 5);
	buttonSizer->Add(cancelButton);
	mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 5);

	dialog->SetSizer(mainSizer);

	if (dialog->ShowModal() == wxID_OK) {
		bool hasChanges = false;

		for (long i = 0; i < hotkeyList->GetItemCount(); ++i) {
			wxListItem item;
			item.SetId(i);
			item.SetColumn(2);
			item.SetMask(wxLIST_MASK_TEXT);
			hotkeyList->GetItem(item);
			wxString displayHk = item.GetText();
			if (displayHk == "(none)") displayHk = "";

			MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(hotkeyList->GetItemData(i));

			auto it = entries_.find(actionId);
			if (it != entries_.end()) {
				wxString entryEffective = it->second.EffectiveKey();
				if (entryEffective != displayHk) {
					if (displayHk.empty()) {
						it->second.overrideKey.reset();
					} else {
						it->second.overrideKey = displayHk;
					}
					hasChanges = true;
				}
			}
		}

		if (hasChanges) {
			SaveCustom();
			RebuildAccelerators(g_gui.root);
			if (g_gui.root) {
				g_gui.root->UpdateMenubar();
			}
		}
	}

	dialog->Destroy();
}
