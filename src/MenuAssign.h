#pragma once

#include "Hotkey.h"

namespace HKS::MenuAssign
{
	// Menus from which a hotkey may be assigned. Favorites uses its own root path;
	// Inventory/Container/Magic/Gift/Barter all share SkyUI's InventoryLists layout.
	[[nodiscard]] bool IsAssignMenuOpen();

	// Identity of the entry currently highlighted in whichever assign-menu is open
	// (form 0 if none). Reads the instance uid (STBuid/STBowner) stamped on the entry by
	// the scaleform callback. Read at modifier-press time so SkyUI type-search on the
	// chord keys can't move the selection out from under us.
	[[nodiscard]] ItemId GetSelectedAssignTarget();
}
