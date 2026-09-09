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
	// Several items are a group and equip as a set -- hand items are dealt out in order
	// (first to the right hand, second to the left) and nothing is ever toggled off.
	void Fire(std::vector<ItemId> a_items);

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
