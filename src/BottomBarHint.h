#pragma once

namespace RE
{
	class IMenu;
}

namespace HKS::BottomBarHint
{
	// Adds "Assign hotkey" (and "Add to group") to the button hints along the bottom of the
	// inventory and magic menus, next to Use / Drop / Unfavorite -- so the feature is
	// discoverable instead of something you have to read the mod page to find out about.
	//
	// SkyUI builds that row in ActionScript: the menu calls its own updateBottomBar() on
	// every selection change, which clears the panel and re-adds a button per available
	// action. We wrap that method on the menu instance and append our own entries after the
	// original has run, exactly the way the keycap rendering wraps formatName. The panel
	// pre-allocates a fixed number of buttons and quietly refuses extras once it is full,
	// so on a crowded row (an enchanted item adds "Charge") our hints are the ones that
	// drop -- which is the right way round.
	//
	// Every other UI would need its own version of this; nothing here is required for the
	// hotkeys themselves to work.
	void Setup(RE::IMenu* a_menu);
}
