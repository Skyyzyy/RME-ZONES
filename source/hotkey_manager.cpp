#include "main.h"
#include "hotkey_manager.h"
#include "hotkey_dialog.h"
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
	struct XmlActionData {
		wxString hotkey;
		wxString help;
		wxString itemName;
	};
	std::unordered_map<std::string, XmlActionData> xmlActions;
	std::unordered_map<std::string, wxString> xmlCategories;

	std::function<void(pugi::xml_node, wxString)> collectActions = [&](pugi::xml_node node, wxString currentMenu) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string actionStr = item.attribute("action").as_string();
			if (!actionStr.empty()) {
				std::string hotkey = item.attribute("hotkey").as_string();
				std::string help = item.attribute("help").as_string();
				std::string name = item.attribute("name").as_string();
				xmlActions[actionStr] = {wxString(hotkey), wxString(help), wxString(name)};
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
		wxString itemName;

		auto xmlIt = xmlActions.find(actionName);
		if (xmlIt != xmlActions.end()) {
			defaultKey = xmlIt->second.hotkey;
			description = xmlIt->second.help;
			itemName = xmlIt->second.itemName;
		}

		ActionInfo info;
		info.name = wxString(actionName);
		info.itemName = itemName;
		info.help = description;
		auto catIt = xmlCategories.find(actionName);
		if (catIt != xmlCategories.end()) {
			info.category = catIt->second;
		}
		actionInfo_[actionId] = info;

		HotkeyEntry entry;
		entry.defaultKey = defaultKey;
		entries_[actionId] = entry;
	}
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

wxString HotkeyManager::GetEffectiveKey(MenuBar::ActionID actionId) const {
	auto it = entries_.find(actionId);
	if (it != entries_.end()) {
		return it->second.EffectiveKey();
	}
	return "";
}

void HotkeyManager::SyncToXml() {
	wxString path = g_gui.GetDataDirectory() + "menubar.xml";
	pugi::xml_document doc;
	if (!doc.load_file(path.mb_str())) {
		return;
	}

	pugi::xml_node menubarNode = doc.child("menubar");
	if (!menubarNode) {
		return;
	}

	// Build a map from action name to XML node
	std::unordered_map<std::string, pugi::xml_node> xmlNodes;
	std::function<void(pugi::xml_node)> collectNodes = [&](pugi::xml_node node) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string action = item.attribute("action").as_string();
			if (!action.empty()) {
				xmlNodes[action] = item;
			}
		}
		for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
			collectNodes(menu);
		}
	};
	collectNodes(menubarNode);

	// Update hotkey attributes for actions with overrides
	for (const auto& [actionId, entry] : entries_) {
		if (!entry.overrideKey.has_value()) {
			continue;
		}

		auto infoIt = actionInfo_.find(actionId);
		if (infoIt == actionInfo_.end()) {
			continue;
		}

		std::string actionName = infoIt->second.name.ToStdString();
		auto nodeIt = xmlNodes.find(actionName);
		if (nodeIt != xmlNodes.end()) {
			std::string effectiveKey = entry.EffectiveKey().ToStdString();
			pugi::xml_attribute attr = nodeIt->second.attribute("hotkey");
			if (attr) {
				attr.set_value(effectiveKey.c_str());
			}
		}
	}

	// Update help text for all actions
	for (const auto& [actionId, info] : actionInfo_) {
		std::string actionName = info.name.ToStdString();
		auto nodeIt = xmlNodes.find(actionName);
		if (nodeIt != xmlNodes.end()) {
			std::string help = info.help.ToStdString();
			pugi::xml_attribute attr = nodeIt->second.attribute("help");
			if (attr) {
				attr.set_value(help.c_str());
			} else {
				nodeIt->second.append_attribute("help") = help.c_str();
			}
		}
	}

	if (!doc.save_file(path.mb_str())) {
		wxLogWarning("Failed to save hotkeys to menubar.xml");
	}
}

void HotkeyManager::ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar) {
	HotkeyDialog dlg(parent, menubar, entries_, actionInfo_);
	if (dlg.ShowModal() == wxID_OK) {
		SyncToXml();
		RebuildAccelerators(g_gui.root);
		menubar->UpdateLabelHotkeys();
		if (g_gui.root) {
			g_gui.root->UpdateMenubar();
		}
	}
}
