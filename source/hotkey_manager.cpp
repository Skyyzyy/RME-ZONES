#include "main.h"
#include "hotkey_manager.h"
#include "hotkey_dialog.h"
#include "settings.h"
#include "gui.h"
#include "gui_ids.h"

#include <functional>

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

	// First pass: collect all actions from menubar.xml with default hotkeys and categories
	std::unordered_map<std::string, std::pair<wxString, wxString>> xmlActions;
	std::unordered_map<std::string, wxString> xmlCategories;

	std::function<void(pugi::xml_node, wxString)> collectActions = [&](pugi::xml_node node, wxString currentMenu) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string actionStr = item.attribute("action").as_string();
			if (!actionStr.empty()) {
				std::string hotkey = item.attribute("hotkey").as_string();
				std::string help = item.attribute("help").as_string();
				xmlActions[actionStr] = {wxString(hotkey), wxString(help)};
				xmlCategories[actionStr] = currentMenu;
			}
		}
		for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
			wxString menuName = wxString(menu.attribute("name").as_string());
			wxString fullPath = currentMenu.empty() ? menuName : currentMenu + " > " + menuName;
			collectActions(menu, fullPath);
			for (pugi::xml_node item = menu.child("item"); item; item = item.next_sibling("item")) {
				std::string action = item.attribute("action").as_string();
				if (!action.empty() && xmlCategories.find(action) == xmlCategories.end()) {
					xmlCategories[action] = currentMenu;
				}
			}
		}
	};
	collectActions(menubarNode, "");

	// Second pass: match XML actions with MainMenuBar ActionID enum
	const auto& actions = menubar->GetActions();
	for (const auto& [actionName, actionPtr] : actions) {
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(actionPtr->id);

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
		auto catIt = xmlCategories.find(actionName);
		if (catIt != xmlCategories.end()) {
			info.category = catIt->second;
		}
		actionInfo_[actionId] = info;

		nameToActionId_[info.name] = actionId;

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
	wxString entryName;
	bool hasEntry = config->GetFirstEntry(entryName, dummy);
	while (hasEntry) {
		auto nameIt = nameToActionId_.find(entryName);
		if (nameIt != nameToActionId_.end()) {
			auto entryIt = entries_.find(nameIt->second);
			if (entryIt != entries_.end()) {
				entryIt->second.overrideKey = config->Read(entryName, "");
			}
		}
		hasEntry = config->GetNextEntry(entryName, dummy);
	}

	config->SetPath("/");
}

void HotkeyManager::SaveCustom() {
	wxConfigBase* config = &Settings::getConfigObject();
	config->SetPath("/Hotkeys/");

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



void HotkeyManager::BuildAcceleratorEntries(std::vector<wxAcceleratorEntry>& accelEntries) const {
	for (const auto& [actionId, entry] : entries_) {
		wxString keyStr = entry.EffectiveKey();
		if (keyStr.empty()) {
			continue;
		}

		wxAcceleratorEntry accel;
		if (accel.FromString(wxString("\t") + keyStr)) {
			int eventId = MAIN_FRAME_MENU + static_cast<int>(actionId);
			accel.Set(accel.GetFlags(), accel.GetKeyCode(), eventId);
			accelEntries.push_back(accel);
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

void HotkeyManager::ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar) {
	HotkeyDialog dlg(parent, menubar, entries_, actionInfo_);
	if (dlg.ShowModal() == wxID_OK) {
		SaveCustom();
		RebuildAccelerators(g_gui.root);
		if (g_gui.root) {
			g_gui.root->UpdateMenubar();
		}
	}
}
