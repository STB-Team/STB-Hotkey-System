#include "MenuAssign.h"

namespace HKS::MenuAssign
{
	namespace
	{
		struct Target
		{
			const char* menu;
			const char* selectedEntry;  // GFx path to the highlighted entry object
		};

		// Favorites has its own root; Inventory/Container/Magic/Gift/Barter all use
		// SkyUI's shared InventoryLists layout, so one path covers them.
		constexpr Target kTargets[] = {
			{ "FavoritesMenu", "_root.MenuHolder.Menu_mc.itemList.selectedEntry" },
			{ "InventoryMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "ContainerMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "MagicMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "GiftMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "BarterMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
		};

		// skyui.defines.Inventory.ICT_ACTIVE_EFFECT. SkyUI's Active Effects tab reuses the
		// very same itemList as spells/items, and its rows carry the SOURCE SPELL's formId
		// -- without this check a chord pressed there binds that spell by accident.
		constexpr std::uint32_t kActiveEffectType = 11;

		std::uint32_t ReadNum(RE::GFxValue& a_entry, const char* a_member)
		{
			RE::GFxValue v;
			return (a_entry.GetMember(a_member, &v) && v.IsNumber()) ? static_cast<std::uint32_t>(v.GetNumber()) : 0;
		}
	}

	bool IsAssignMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		for (const auto& t : kTargets) {
			if (ui->IsMenuOpen(t.menu)) {
				return true;
			}
		}
		return false;
	}

	ItemId GetSelectedAssignTarget()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return {};
		}
		for (const auto& t : kTargets) {
			auto menu = ui->GetMenu(t.menu);
			if (!menu || !menu->uiMovie) {
				continue;
			}
			RE::GFxValue entry;
			if (!menu->uiMovie->GetVariable(&entry, t.selectedEntry) || !entry.IsObject()) {
				continue;
			}
			const auto fid = static_cast<RE::FormID>(ReadNum(entry, "formId"));
			if (!fid) {
				continue;
			}
			if (ReadNum(entry, "type") == kActiveEffectType) {
				continue;  // an active effect is not something you can equip
			}
			ItemId id;
			id.form = fid;
			id.ench = static_cast<RE::FormID>(ReadNum(entry, "STBench"));
			id.uid = static_cast<std::uint16_t>(ReadNum(entry, "STBuid"));
			id.health = static_cast<std::int32_t>(ReadNum(entry, "STBhealth"));
			return id;
		}
		return {};
	}
}
