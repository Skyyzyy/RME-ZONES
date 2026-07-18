#include "hotkey_dialog.h"

#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/statline.h>
#include <wx/textdlg.h>

#include <algorithm>

static bool ValidateHotkeyString(const wxString& hotkey, wxString& error) {
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

bool HotkeyDialog::ContainsIgnoreCase(const wxString& source, const wxString& search) {
	if (search.empty()) return true;
	return source.Lower().Find(search.Lower()) != wxNOT_FOUND;
}

HotkeyDialog::HotkeyDialog(wxWindow* parent, MainMenuBar* menubar,
	std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries,
	std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo)
	: wxDialog(parent, wxID_ANY, "Hotkey Configuration", wxDefaultPosition, wxSize(850, 550))
	, menubar_(menubar)
	, entries_(entries)
	, actionInfo_(actionInfo)
{
	BuildDisplayItems();
	BuildMenuHelpEntries();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Edit area
	wxBoxSizer* editSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* label = new wxStaticText(this, wxID_ANY, "Hotkey:");
	hotkeyEdit_ = new wxTextCtrl(this, wxID_ANY, "",
		wxDefaultPosition, wxSize(150, -1), wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	hotkeyEdit_->SetEditable(false);

	hotkeyEdit_->Bind(wxEVT_KEY_DOWN, &HotkeyDialog::OnKeyDown, this);

	hotkeyEdit_->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_BACK) {
			hotkeyEdit_->SetValue("");
			currentModifiers_.clear();
		}
		event.Skip();
	});

	wxButton* setButton = new wxButton(this, wxID_ANY, "Set");
	editSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	editSizer->Add(hotkeyEdit_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	editSizer->Add(setButton, 0);

	mainSizer->Add(editSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

	// Hotkey Search
	wxBoxSizer* hotkeySearchSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* hotkeySearchLabel = new wxStaticText(this, wxID_ANY, "Hotkey Search:");
	hotkeySearch_ = new wxTextCtrl(this, wxID_ANY, "",
		wxDefaultPosition, wxSize(200, -1));
	hotkeySearchSizer->Add(hotkeySearchLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	hotkeySearchSizer->Add(hotkeySearch_, 1, wxALIGN_CENTER_VERTICAL);

	mainSizer->Add(hotkeySearchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

	mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

	// Help Search
	wxBoxSizer* helpSearchSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* helpSearchLabel = new wxStaticText(this, wxID_ANY, "Help Search:");
	helpSearch_ = new wxTextCtrl(this, wxID_ANY, "",
		wxDefaultPosition, wxSize(200, -1));
	helpSearchSizer->Add(helpSearchLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	helpSearchSizer->Add(helpSearch_, 1, wxALIGN_CENTER_VERTICAL);

	mainSizer->Add(helpSearchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

	mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

	// Hotkey list
	hotkeyList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	hotkeyList_->InsertColumn(0, "Menu", wxLIST_FORMAT_LEFT, 170);
	hotkeyList_->InsertColumn(1, "Action", wxLIST_FORMAT_LEFT, 170);
	hotkeyList_->InsertColumn(2, "Hotkey", wxLIST_FORMAT_LEFT, 110);
	hotkeyList_->InsertColumn(3, "Help", wxLIST_FORMAT_LEFT, 300);
	PopulateList();

	// Handle list selection
	hotkeyList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
		wxListItem item;
		item.SetId(event.GetIndex());
		item.SetColumn(2);
		item.SetMask(wxLIST_MASK_TEXT);
		hotkeyList_->GetItem(item);
		wxString hk = item.GetText();
		if (hk == "(none)") hk = "";
		hotkeyEdit_->SetValue(hk);
	});

	// Double-click to edit help text
	hotkeyList_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
		long itemIndex = event.GetIndex();
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(hotkeyList_->GetItemData(itemIndex));
		auto infoIt = actionInfo_.find(actionId);
		if (infoIt == actionInfo_.end()) return;

		wxTextEntryDialog dlg(this, "Edit help text:", "Edit Help", infoIt->second.help);
		if (dlg.ShowModal() == wxID_OK) {
			wxString newHelp = dlg.GetValue();
			infoIt->second.help = newHelp;
			hotkeyList_->SetItem(itemIndex, 3, newHelp);
		}
	});

	setButton->Bind(wxEVT_BUTTON, &HotkeyDialog::OnSetButton, this);

	hotkeySearch_->Bind(wxEVT_TEXT, &HotkeyDialog::OnHotkeySearch, this);
	helpSearch_->Bind(wxEVT_TEXT, &HotkeyDialog::OnHelpSearch, this);

	mainSizer->Add(hotkeyList_, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* saveButton = new wxButton(this, wxID_OK, "Save");
	wxButton* cancelButton = new wxButton(this, wxID_CANCEL, "Cancel");
	buttonSizer->Add(saveButton, 0, wxRIGHT, 5);
	buttonSizer->Add(cancelButton);
	mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 5);

	SetSizer(mainSizer);
}

void HotkeyDialog::BuildDisplayItems() {
	const auto& actions = menubar_->GetActions();
	displayItems_.clear();
	displayItems_.reserve(actions.size());

	for (const auto& [actionName, actionPtr] : actions) {
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(actionPtr->id);
		auto entryIt = entries_.find(actionId);
		if (entryIt == entries_.end()) {
			continue;
		}

		auto infoIt = actionInfo_.find(actionId);
		wxString category = infoIt != actionInfo_.end() ? infoIt->second.category : wxString();
		wxString help = infoIt != actionInfo_.end() ? infoIt->second.help : wxString();

		displayItems_.push_back({
			category,
			actionId,
			wxString(actionName),
			entryIt->second.EffectiveKey(),
			help
		});
	}

	std::sort(displayItems_.begin(), displayItems_.end(),
		[](const DisplayItem& a, const DisplayItem& b) {
			if (a.category != b.category) return a.category < b.category;
			return a.actionName < b.actionName;
		});
}

void HotkeyDialog::BuildMenuHelpEntries() {
	menuHelpEntries_.clear();
	menuHelpEntries_.reserve(actionInfo_.size());

	for (const auto& [actionId, info] : actionInfo_) {
		auto entryIt = entries_.find(actionId);
		wxString shortcut = entryIt != entries_.end() ? entryIt->second.EffectiveKey() : wxString();

		menuHelpEntries_.push_back({
			info.category,
			info.itemName,
			info.name,
			info.help,
			shortcut
		});
	}
}

void HotkeyDialog::PopulateList() {
	hotkeyList_->DeleteAllItems();

	wxString hotkeySearch = hotkeySearch_->GetValue().Trim();
	wxString helpSearch = helpSearch_->GetValue().Trim();

	// Collect actions matching help search
	std::set<wxString> helpMatches;
	if (!helpSearch.empty()) {
		for (const auto& entry : menuHelpEntries_) {
			if (ContainsIgnoreCase(entry.menu, helpSearch) ||
				ContainsIgnoreCase(entry.text, helpSearch) ||
				ContainsIgnoreCase(entry.action, helpSearch) ||
				ContainsIgnoreCase(entry.help, helpSearch) ||
				ContainsIgnoreCase(entry.shortcut, helpSearch)) {
				helpMatches.insert(entry.action);
			}
		}
	}

	for (size_t i = 0; i < displayItems_.size(); ++i) {
		const auto& item = displayItems_[i];

		if (!hotkeySearch.empty()) {
			if (!ContainsIgnoreCase(item.category, hotkeySearch) &&
				!ContainsIgnoreCase(item.actionName, hotkeySearch) &&
				!ContainsIgnoreCase(item.hotkey, hotkeySearch) &&
				!ContainsIgnoreCase(item.help, hotkeySearch)) {
				continue;
			}
		}

		if (!helpSearch.empty()) {
			if (helpMatches.find(item.actionName) == helpMatches.end()) {
				continue;
			}
		}

		long idx = hotkeyList_->InsertItem(hotkeyList_->GetItemCount(), item.category);
		hotkeyList_->SetItem(idx, 1, item.actionName);
		hotkeyList_->SetItemPtrData(idx, static_cast<long>(item.action));
		wxString hk = item.hotkey;
		if (hk.empty()) {
			hk = "(none)";
		}
		hotkeyList_->SetItem(idx, 2, hk);
		hotkeyList_->SetItem(idx, 3, item.help);
	}
}

void HotkeyDialog::OnHotkeySearch(wxCommandEvent&) {
	PopulateList();
}

void HotkeyDialog::OnHelpSearch(wxCommandEvent&) {
	PopulateList();
}

void HotkeyDialog::OnKeyDown(wxKeyEvent& event) {
	int keyCode = event.GetKeyCode();

	if (keyCode == WXK_ESCAPE) {
		event.Skip();
		return;
	}

	if (keyCode == WXK_SHIFT || keyCode == WXK_CONTROL || keyCode == WXK_ALT) {
		currentModifiers_.insert(keyCode);
		wxString currentValue = hotkeyEdit_->GetValue();
		wxString modStr;
		if (keyCode == WXK_SHIFT) modStr = "Shift+";
		else if (keyCode == WXK_CONTROL) modStr = "Ctrl+";
		else if (keyCode == WXK_ALT) modStr = "Alt+";

		if (!currentValue.Contains(modStr)) {
			if (!currentValue.empty() && !currentValue.EndsWith("+")) {
				currentValue += "+";
			}
			currentValue += modStr;
			hotkeyEdit_->SetValue(currentValue);
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

		static const std::unordered_map<int, const char*> kSpecialKeys = {
			{WXK_SPACE, "Space"}, {WXK_TAB, "Tab"}, {WXK_RETURN, "Enter"},
			{WXK_ESCAPE, "Esc"}, {WXK_LEFT, "Left"}, {WXK_RIGHT, "Right"},
			{WXK_UP, "Up"}, {WXK_DOWN, "Down"},
			{WXK_HOME, "Home"}, {WXK_END, "End"},
			{WXK_PAGEUP, "PgUp"}, {WXK_PAGEDOWN, "PgDn"},
			{WXK_INSERT, "Insert"}, {WXK_DELETE, "Delete"}
		};

		wxString finalKey;
		if (keyCode >= WXK_F1 && keyCode <= WXK_F12) {
			finalKey = wxString::Format("F%d", keyCode - WXK_F1 + 1);
		} else {
			auto keyIt = kSpecialKeys.find(keyCode);
			if (keyIt != kSpecialKeys.end()) {
				finalKey = keyIt->second;
			} else {
				finalKey = wxString(static_cast<wxChar>(keyCode));
			}
		}

		wxString currentValue = hotkeyEdit_->GetValue();
		if (!currentValue.empty() && !currentValue.EndsWith("+")) {
			size_t lastPlus = currentValue.find_last_of('+');
			if (lastPlus != wxString::npos) {
				currentValue = currentValue.substr(0, lastPlus + 1);
			} else {
				currentValue = "";
			}
		}
		currentValue += finalKey;
		hotkeyEdit_->SetValue(currentValue);
		event.Skip(false);
		return;
	}

	if (keyCode != WXK_BACK) {
		event.Skip(false);
	}
}

void HotkeyDialog::OnSetButton(wxCommandEvent&) {
	wxString newHotkey = hotkeyEdit_->GetValue();
	wxString error;

	long selectedIndex = hotkeyList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selectedIndex == -1) {
		wxMessageBox("Please select an action first", "Error", wxOK | wxICON_ERROR);
		return;
	}

	MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(hotkeyList_->GetItemData(selectedIndex));

	if (!ValidateHotkeyString(newHotkey, error)) {
		wxMessageBox(error, "Invalid Hotkey", wxOK | wxICON_ERROR);
		return;
	}

	// Check for duplicates
	for (const auto& pair : entries_) {
		if (pair.first != actionId && !pair.second.EffectiveKey().empty() &&
			pair.second.EffectiveKey() == newHotkey && !newHotkey.empty()) {
			auto infoIt = actionInfo_.find(pair.first);
			wxString conflictName = infoIt != actionInfo_.end() ? infoIt->second.name : wxString("Unknown");

			wxMessageDialog* confirmDialog = new wxMessageDialog(
				this,
				"This hotkey is already assigned to: " + conflictName +
				"\n\nDo you want to reassign it?",
				"Duplicate Hotkey",
				wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
			);

			if (confirmDialog->ShowModal() == wxID_YES) {
				entries_[pair.first].overrideKey = "";
			} else {
				delete confirmDialog;
				return;
			}
			delete confirmDialog;
			break;
		}
	}

	if (newHotkey.empty()) {
		entries_[actionId].overrideKey = "";
	} else {
		entries_[actionId].overrideKey = newHotkey;
	}

	hotkeyList_->SetItem(selectedIndex, 2, newHotkey.empty() ? wxString("(none)") : newHotkey);
}
