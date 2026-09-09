#pragma once

#include "Hotkey.h"

#include <vector>

namespace HKS::EquipDispatch
{
	// Act on the items bound to one hotkey. Safe to call from the input thread: the work
	// is re-resolved by FormID and run on the main thread via the SKSE task queue. We never
	// carry a raw ExtraDataList pointer across the queue (that pointer can be freed when
	// stacks are re-split -- the reference mod's deferred-equip use-after-free crash).
	//
	// One item keeps the vanilla-favourites feel: press to equip, press again to put away.
	// Several items are a group, which toggles as a set: hand items are dealt out in order
	// (first to the right hand, second to the left), and once the whole set is on, the next
	// press takes it off.
	void Fire(std::vector<ItemId> a_items);

	// Which hand(s) the player is holding this form in right now, as a HandMask. Read at
	// assignment time and stored on the binding, so the hotkey can put the form back where
	// it was instead of leaving the choice to the engine. kHandNone for anything that is
	// not in a hand -- armour, ammo, and anything not currently equipped.
	[[nodiscard]] std::uint8_t CurrentHands(RE::TESForm* a_form);

	// True for forms that go in the Voice slot (shouts and powers/lesser-powers). These
	// are the ones that can be "equip + instantly cast" via the ShoutHandler hook.
	[[nodiscard]] bool IsVoiceForm(RE::TESForm* a_form);

	// Equip synchronously (caller must be on the main thread, e.g. inside an input
	// handler). Used by the ShoutHandler hook so the cast that follows sees the new
	// shout/power already equipped. Returns false when nothing was equipped (binding
	// dropped, or the player no longer has the form) -- the caller must then NOT let the
	// shout key act, or vanilla would fire whatever is still in the voice slot.
	[[nodiscard]] bool EquipNow(const ItemId& a_id);
}
