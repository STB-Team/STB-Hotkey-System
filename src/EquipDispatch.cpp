#include "EquipDispatch.h"

#include "Favorites.h"
#include "HotkeyManager.h"
#include "Settings.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

namespace HKS::EquipDispatch
{
	namespace
	{
		// Core equip-slot forms (Skyrim.esm). Looked up by ID so we never go through
		// GetObject<T>'s RTTI cast on a possibly-bad default-object entry.
		RE::BGSEquipSlot* EquipSlot(RE::FormID a_id)
		{
			auto* form = RE::TESForm::LookupByID(a_id);
			return form ? form->As<RE::BGSEquipSlot>() : nullptr;
		}

		void EquipSpellForm(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			RE::SpellItem* a_spell, std::uint8_t a_hands)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;
			constexpr RE::FormID kBoth = 0x13F45;

			// Decide two-handedness by comparing the spell's equip-slot POINTER to the
			// both-hands slot -- never dereference the spell's slot (a custom/broken spell
			// can carry a dangling equip-slot pointer; IsTwoHanded() would crash on it).
			auto* spellSlot = a_spell->GetEquipSlot();
			auto* bothSlot = EquipSlot(kBoth);
			if (spellSlot && spellSlot == bothSlot) {
				a_em->EquipSpell(a_player, a_spell, bothSlot);
				return;
			}

			// Remembered hand wins: the spell goes back exactly where it was when the key
			// was assigned, every press. Held in both hands at that moment means both hands
			// on one press, which is the thing the alternating fallback below can only get
			// to on the second.
			if (a_hands != kHandNone) {
				if (a_hands & kHandRight) {
					a_em->EquipSpell(a_player, a_spell, EquipSlot(kRight));
				}
				if (a_hands & kHandLeft) {
					a_em->EquipSpell(a_player, a_spell, EquipSlot(kLeft));
				}
				return;
			}

			// No preference recorded: mirror vanilla favorites, prefer the empty/other hand.
			auto& rt = a_player->GetActorRuntimeData();
			if (rt.selectedSpells[RE::Actor::SlotTypes::kLeftHand] != a_spell) {
				a_em->EquipSpell(a_player, a_spell, EquipSlot(kLeft));
			} else if (rt.selectedSpells[RE::Actor::SlotTypes::kRightHand] != a_spell) {
				a_em->EquipSpell(a_player, a_spell, EquipSlot(kRight));
			}
		}

		// Read one ExtraDataList's instance identity (ench/uid/health) for comparison.
		void ReadListId(RE::ExtraDataList* a_xl, RE::FormID& a_ench, std::uint16_t& a_uid, std::int32_t& a_health)
		{
			a_ench = 0;
			a_uid = 0;
			a_health = 0;
			if (auto* e = a_xl->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) {
				a_ench = e->enchantment->GetFormID();
			}
			if (auto* u = a_xl->GetByType<RE::ExtraUniqueID>()) {
				a_uid = u->uniqueID;
			}
			if (auto* h = a_xl->GetByType<RE::ExtraHealth>()) {
				a_health = static_cast<std::int32_t>(std::lround(h->health * 100.0f));
			}
		}

		// Find the inventory ExtraDataList for the exact instance the hotkey is bound to
		// (the enchanted/tempered copy, not a plain one of the same base form). Returns
		// nullptr for fungible binds (no instance data), letting the game pick any copy.
		RE::ExtraDataList* FindInstanceList(RE::TESBoundObject* a_bound, const ItemId& a_id)
		{
			// uid ignored (see ItemId::Same) -- it never identifies a distinct row, and old
			// co-saves may still carry a stale one.
			if (a_id.ench == 0 && a_id.health == 0) {
				return nullptr;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench == a_id.ench && health == a_id.health) {
						return xl;
					}
				}
			}
			return nullptr;
		}

		// Count owned copies of a_bound that are interchangeable with the bound item --
		// same enchantment and temper, ANY ExtraUniqueID. uid is deliberately ignored: it
		// is only a per-instance tag. Vanilla stacks plain copies (uid 0), but mods like
		// Wheeler stamp every weapon/armor copy with a unique ExtraUniqueID, so two
		// otherwise-identical swords would look like distinct instances. For the dual-wield
		// decision they are the same item.
		std::int32_t CountMatchingInstances(RE::TESBoundObject* a_bound, RE::FormID a_ench, std::int32_t a_health)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return 0;
			}

			std::int32_t total = 0;
			auto counts = player->GetInventoryCounts(
				[&](RE::TESBoundObject& a_obj) { return std::addressof(a_obj) == a_bound; });
			if (!counts.empty()) {
				total = counts.begin()->second;
			}
			if (total <= 0) {
				return 0;
			}

			auto* changes = player->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				// No per-instance data at all -> every copy is plain (ench/health 0).
				return (a_ench == 0 && a_health == 0) ? total : 0;
			}

			std::int32_t extraTotal = 0;
			std::int32_t matchedExtra = 0;
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					const auto c = xl->GetCount();
					extraTotal += c;
					if (ench == a_ench && health == a_health) {
						matchedExtra += c;
					}
				}
			}
			// Copies not covered by any extra list are plain (ench/health 0).
			const std::int32_t plain = total - extraTotal;
			std::int32_t matched = matchedExtra;
			if (plain > 0 && a_ench == 0 && a_health == 0) {
				matched += plain;
			}
			return matched;
		}

		// Return an inventory ExtraDataList matching (ench, health) -- any uid -- that is
		// NOT currently worn, so the dual-wield path equips a *different* physical copy to
		// the free hand. nullptr means "no distinct spare list" (plain stacked copies), in
		// which case the caller lets the engine pick a copy.
		RE::ExtraDataList* FindUnwornInstance(RE::TESBoundObject* a_bound, RE::FormID a_ench,
			std::int32_t a_health, RE::ExtraDataList* a_skip = nullptr)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl || xl == a_skip) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench != a_ench || health != a_health) {
						continue;
					}
					if (xl->HasType(RE::ExtraDataType::kWorn) || xl->HasType(RE::ExtraDataType::kWornLeft)) {
						continue;
					}
					return xl;
				}
			}
			return nullptr;
		}

		// Put an item in the hand(s) the bind remembers. Both hands means the player was
		// dual-wielding it when the key was assigned, so a second interchangeable copy goes
		// to the left -- FindUnwornInstance skips the one already going to the right, and a
		// null list is fine: plain stacked copies carry no extra data and the engine picks.
		void EquipToHands(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			RE::TESBoundObject* a_bound, const ItemId& a_id, RE::ExtraDataList* a_xl)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;

			if (a_id.hands == kHandBoth) {
				a_em->EquipObject(a_player, a_bound, a_xl, 1, EquipSlot(kRight));
				if (CountMatchingInstances(a_bound, a_id.ench, a_id.health) >= 2) {
					a_em->EquipObject(a_player, a_bound,
						FindUnwornInstance(a_bound, a_id.ench, a_id.health, a_xl), 1, EquipSlot(kLeft));
				}
				return;
			}
			a_em->EquipObject(a_player, a_bound, a_xl, 1,
				EquipSlot((a_id.hands & kHandRight) != 0 ? kRight : kLeft));
		}

		// Is the bound item on the player right now?
		//
		// The hand slots alone are not the answer. They only ever hold weapons, shields,
		// torches and spells, so an armour piece looks "not equipped" no matter what -- and
		// the toggle below then tried to equip it again instead of taking it off. That was
		// the bug: cuirasses and helmets could be put on with their hotkey but never off.
		//
		// a_xl is the exact instance the bind points at when there is one; otherwise any
		// worn copy of the base object counts, which is what a fungible bind means.
		bool IsWornNow(RE::PlayerCharacter* a_player, RE::TESForm* a_form,
			RE::TESBoundObject* a_bound, const ItemId& a_id, RE::ExtraDataList* a_xl)
		{
			if (a_xl) {
				return a_xl->HasType(RE::ExtraDataType::kWorn) ||
				       a_xl->HasType(RE::ExtraDataType::kWornLeft);
			}
			auto* proc = a_player->GetActorRuntimeData().currentProcess;
			if (proc && (proc->GetEquippedRightHand() == a_form || proc->GetEquippedLeftHand() == a_form)) {
				return true;
			}
			auto* changes = a_player->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return false;
			}
			// Match the enchant/temper the bind names, not just the base object: an
			// enchanted cuirass and a plain one share a row, and taking the enchanted one
			// off because the plain one's hotkey was pressed is the same class of mistake
			// the instance-precise toggle exists to avoid. Wearing anything at all creates
			// the extra-data list that carries kWorn, so a plain worn copy is found here.
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl || !(xl->HasType(RE::ExtraDataType::kWorn) ||
									xl->HasType(RE::ExtraDataType::kWornLeft))) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench == a_id.ench && health == a_id.health) {
						return true;
					}
				}
			}
			return false;
		}

		// Part of a "loadout" for the group toggle: something that stays on until it is
		// taken off. Potions, food and ingredients are used up instead, so they never keep
		// a group from reading as fully equipped -- and are left alone when it is stripped.
		bool IsLoadoutGear(RE::TESForm* a_form)
		{
			return a_form->As<RE::TESBoundObject>() != nullptr &&
			       !a_form->Is(RE::FormType::AlchemyItem) &&
			       !a_form->Is(RE::FormType::Ingredient);
		}

		void EquipForm(const ItemId& a_id)
		{
			auto* form = RE::TESForm::LookupByID(a_id.form);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!player || !em || !form) {
				return;
			}

			switch (form->GetFormType()) {
			case RE::FormType::Spell:
				if (auto* spell = form->As<RE::SpellItem>()) {
					EquipSpellForm(player, em, spell, a_id.hands);
				}
				break;

			case RE::FormType::Shout:
				if (auto* shout = form->As<RE::TESShout>()) {
					em->EquipShout(player, shout);
				}
				break;

			default:
				{
					auto* bound = form->As<RE::TESBoundObject>();
					if (!bound) {
						break;
					}

					auto* proc = player->GetActorRuntimeData().currentProcess;
					const bool inRight = proc && proc->GetEquippedRightHand() == form;
					const bool inLeft = proc && proc->GetEquippedLeftHand() == form;

					auto* weap = form->As<RE::TESObjectWEAP>();
					const bool dualWield = weap &&
					                       (weap->IsOneHandedSword() || weap->IsOneHandedDagger() ||
					                        weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsStaff());

					// --- Dual wield (runs BEFORE the instance-precise toggle) ---
					// One-handed weapon already held in exactly one hand, with a second
					// interchangeable copy (same enchant/temper, any ExtraUniqueID) in the
					// pack -> equip that copy to the free hand, matching vanilla favorites
					// (two presses = dual wield). Placed first, and keyed on ench/temper
					// rather than uid, so it still fires when a mod (e.g. Wheeler) stamps a
					// unique ExtraUniqueID on every weapon copy -- otherwise our bind reads
					// as one distinct instance and the toggle below just puts it away.
					if (a_id.hands == kHandNone && dualWield && (inRight != inLeft) &&
						CountMatchingInstances(bound, a_id.ench, a_id.health) >= 2) {
						constexpr RE::FormID kRight = 0x13F42;
						constexpr RE::FormID kLeft = 0x13F43;
						auto* freeXl = FindUnwornInstance(bound, a_id.ench, a_id.health);
						em->EquipObject(player, bound, freeXl, 1, EquipSlot(inRight ? kLeft : kRight));
						break;
					}

					auto* xl = FindInstanceList(bound, a_id);

					// --- Remembered hand ---
					// Assigned while the item was in hand, so it goes back to that hand
					// rather than to whichever one the engine feels like. Still a toggle:
					// pressing it while worn puts it away, same as everything else.
					if (a_id.hands != kHandNone) {
						if (IsWornNow(player, form, bound, a_id, xl)) {
							em->UnequipObject(player, bound, xl);
						} else {
							EquipToHands(player, em, bound, a_id, xl);
						}
						break;
					}

					// --- Instance-precise bind (enchanted/tempered/unique copy) ---
					// You only ever own the one specific instance, so a plain toggle is
					// correct. Test "worn" on the EXACT instance, not the base form;
					// otherwise pressing the enchanted sword's hotkey while a plain one
					// (same base) is in hand reads as "already worn" and unequips instead
					// of switching.
					if (xl) {
						if (IsWornNow(player, form, bound, a_id, xl)) {
							em->UnequipObject(player, bound, xl);
						} else {
							em->EquipObject(player, bound, xl);
						}
						break;
					}

					// --- Fungible bind (plain copies of a base form) ---
					// Default toggle: equip if nothing of the kind is on, else take it off.
					// "On" has to cover the armour slots too, not just the hands.
					if (IsWornNow(player, form, bound, a_id, nullptr)) {
						em->UnequipObject(player, bound, nullptr);
					} else {
						em->EquipObject(player, bound, nullptr);
					}
				}
				break;
			}
		}

		// Slots a group member can claim. Tracked per hand rather than as a count so a
		// shield or torch (which can only go left) doesn't consume the right hand.
		struct Hands
		{
			bool right = false;
			bool left = false;
		};

		// Equip one member of a group. No toggling here -- EquipSet decides that for the set
		// as a whole. A member that remembers a hand goes back to it and claims that hand;
		// the rest take the right hand first, then the left, in the order they were added.
		void EquipGroupMember(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			const ItemId& a_id, Hands& a_hands)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;
			constexpr RE::FormID kBoth = 0x13F45;

			auto* form = RE::TESForm::LookupByID(a_id.form);
			if (!form) {
				return;
			}

			// Voice forms never touch the hands.
			if (auto* shout = form->As<RE::TESShout>()) {
				a_em->EquipShout(a_player, shout);
				return;
			}
			if (auto* spell = form->As<RE::SpellItem>()) {
				if (IsVoiceForm(form)) {
					a_em->EquipSpell(a_player, spell, spell->GetEquipSlot());
					return;
				}
				if (a_id.hands != kHandNone) {
					EquipSpellForm(a_player, a_em, spell, a_id.hands);
					a_hands.right = a_hands.right || (a_id.hands & kHandRight) != 0;
					a_hands.left = a_hands.left || (a_id.hands & kHandLeft) != 0;
					return;
				}
				// Compare the equip-slot POINTER rather than calling IsTwoHanded(): a broken
				// or custom spell can carry a dangling slot pointer.
				if (spell->GetEquipSlot() && spell->GetEquipSlot() == EquipSlot(kBoth)) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kBoth));
					a_hands.right = a_hands.left = true;
				} else if (!a_hands.right) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			auto* bound = form->As<RE::TESBoundObject>();
			if (!bound) {
				return;
			}
			// Pick the exact instance the bind points at (enchanted/tempered copy); null for
			// a fungible bind, which lets the engine take any copy.
			auto* xl = FindInstanceList(bound, a_id);

			if (auto* weap = form->As<RE::TESObjectWEAP>()) {
				const bool oneHanded = weap->IsOneHandedSword() || weap->IsOneHandedDagger() ||
				                       weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsStaff();
				if (oneHanded && a_id.hands != kHandNone) {
					EquipToHands(a_player, a_em, bound, a_id, xl);
					a_hands.right = a_hands.right || (a_id.hands & kHandRight) != 0;
					a_hands.left = a_hands.left || (a_id.hands & kHandLeft) != 0;
					return;
				}
				if (!oneHanded) {  // greatsword, bow, crossbow -- takes everything
					a_em->EquipObject(a_player, bound, xl);
					a_hands.right = a_hands.left = true;
				} else if (!a_hands.right) {
					a_em->EquipObject(a_player, bound, xl, 1, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					a_em->EquipObject(a_player, bound, xl, 1, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			// Shields and torches are left-hand only; they must not eat the right hand, so a
			// group of "shield, sword" still puts the sword where it belongs.
			auto*      armo = form->As<RE::TESObjectARMO>();
			const bool leftOnly = (armo && armo->IsShield()) || form->Is(RE::FormType::Light);
			if (leftOnly) {
				if (!a_hands.left) {
					a_em->EquipObject(a_player, bound, xl);
					a_hands.left = true;
				}
				return;
			}

			// A scroll is cast from a hand like a spell.
			if (form->Is(RE::FormType::Scroll)) {
				if (!a_hands.right) {
					a_em->EquipObject(a_player, bound, xl, 1, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					a_em->EquipObject(a_player, bound, xl, 1, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			// Armour, ammo, potions, food -- no hand bookkeeping. Potions and food are
			// consumed here, which is what a group like "armour + healing potion" is for.
			a_em->EquipObject(a_player, bound, xl);
		}

		// Take the whole set off. Consumables are skipped -- stripping a loadout must not
		// drink the potion that is on the same key. Spells and shouts stay put: the engine
		// offers no "unequip spell", and vanilla favorites never took one off either.
		void UnequipSet(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			const std::vector<ItemId>& a_items)
		{
			for (const auto& id : a_items) {
				auto* form = RE::TESForm::LookupByID(id.form);
				if (!form || !IsLoadoutGear(form)) {
					continue;
				}
				auto* bound = form->As<RE::TESBoundObject>();
				a_em->UnequipObject(a_player, bound, FindInstanceList(bound, id));
			}
		}

		void EquipSet(const std::vector<ItemId>& a_items)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!player || !em) {
				return;
			}

			// A group toggles like a single bind does, just on the whole set: everything in
			// it already on -> take it all off, anything missing -> put the set on. Without
			// this, gear that was perfectly removable on its own became stuck the moment it
			// shared a key with something else.
			bool anyGear = false;
			bool allWorn = true;
			for (const auto& id : a_items) {
				auto* form = RE::TESForm::LookupByID(id.form);
				if (!form || !IsLoadoutGear(form)) {
					continue;
				}
				anyGear = true;
				auto* bound = form->As<RE::TESBoundObject>();
				if (!IsWornNow(player, form, bound, id, FindInstanceList(bound, id))) {
					allWorn = false;
					break;
				}
			}
			if (anyGear && allWorn) {
				UnequipSet(player, em, a_items);
				return;
			}

			Hands hands;
			for (const auto& id : a_items) {
				EquipGroupMember(player, em, id, hands);
			}
		}
	}

	std::uint8_t CurrentHands(RE::TESForm* a_form)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_form) {
			return kHandNone;
		}
		std::uint8_t mask = kHandNone;
		// Magic lives in selectedSpells, not in the process's hand slots -- the same place
		// EquipSpellForm reads when it decides which hand is free.
		if (auto* spell = a_form->As<RE::SpellItem>()) {
			auto& rt = player->GetActorRuntimeData();
			if (rt.selectedSpells[RE::Actor::SlotTypes::kRightHand] == spell) {
				mask |= kHandRight;
			}
			if (rt.selectedSpells[RE::Actor::SlotTypes::kLeftHand] == spell) {
				mask |= kHandLeft;
			}
			return mask;
		}
		auto* proc = player->GetActorRuntimeData().currentProcess;
		if (!proc) {
			return kHandNone;
		}
		if (proc->GetEquippedRightHand() == a_form) {
			mask |= kHandRight;
		}
		if (proc->GetEquippedLeftHand() == a_form) {
			mask |= kHandLeft;
		}
		return mask;
	}

	bool IsVoiceForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		if (a_form->Is(RE::FormType::Shout)) {
			return true;
		}
		if (auto* spell = a_form->As<RE::SpellItem>()) {
			const auto* slot = spell->GetEquipSlot();
			return slot && slot->GetFormID() == 0x25BEE;  // Voice slot
		}
		return false;
	}

	void Fire(std::vector<ItemId> a_items)
	{
		if (a_items.empty()) {
			return;
		}
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask([items = std::move(a_items)]() {
			// Reconcile before equipping. An item the player un-favorited loses its place in
			// the bind; one they merely ran out of keeps it (PickupWatch restores the star
			// when it comes back), it is just skipped this press.
			std::vector<ItemId> live;
			live.reserve(items.size());
			for (const auto& id : items) {
				const auto state = Favorites::Query(id.form);
				if (state == Favorites::State::kUnfavorited) {
					HotkeyManager::GetSingleton()->RemoveByItem(id);
					logger::info("fire {:08X}: un-favorited -> dropped from its hotkey", id.form);
					continue;
				}
				if (state == Favorites::State::kAbsent) {
					if (Settings::DebugLog()) {
						logger::info("fire {:08X}: none held -> skipped, binding kept", id.form);
					}
					continue;
				}
				live.push_back(id);
			}
			if (live.empty()) {
				return;
			}

			if (Settings::DebugLog()) {
				logger::info("equip fire -> {} item(s), first form {:08X} ench {:08X} hp {}",
					live.size(), live.front().form, live.front().ench, live.front().health);
			}

			// One item keeps the toggle; a group is a loadout and only ever equips.
			if (live.size() == 1) {
				EquipForm(live.front());
			} else {
				EquipSet(live);
			}
		});
	}

	namespace
	{
		// Forms an API consumer equipped a moment ago, with the time it happened. Small and
		// short-lived, so a flat list beats a map. Guarded because EquipNow is called from
		// an input handler while our own sink reads it from the same dispatch.
		constexpr auto kClaimWindow = std::chrono::milliseconds(100);

		std::mutex                                                                       g_claimLock;
		std::vector<std::pair<RE::FormID, std::chrono::steady_clock::time_point>>        g_claims;
	}

	bool EquipNow(const ItemId& a_id)
	{
		if (!a_id) {
			return false;
		}
		const auto state = Favorites::Query(a_id.form);
		if (state == Favorites::State::kUnfavorited) {
			HotkeyManager::GetSingleton()->RemoveByItem(a_id);
			logger::info("EquipNow {:08X}: un-favorited -> binding removed", a_id.form);
			return false;
		}
		if (state == Favorites::State::kAbsent) {
			// The player no longer holds it (used the last one, lost the power).
			if (Settings::DebugLog()) {
				logger::info("EquipNow {:08X}: player no longer has it", a_id.form);
			}
			return false;
		}

		EquipForm(a_id);

		const auto now = std::chrono::steady_clock::now();
		{
			std::scoped_lock lk(g_claimLock);
			std::erase_if(g_claims, [&](const auto& c) { return now - c.second > kClaimWindow; });
			g_claims.emplace_back(a_id.form, now);
		}
		return true;
	}

	bool IsClaimed(const ItemId& a_id)
	{
		const auto       now = std::chrono::steady_clock::now();
		std::scoped_lock lk(g_claimLock);
		std::erase_if(g_claims, [&](const auto& c) { return now - c.second > kClaimWindow; });
		return std::any_of(g_claims.begin(), g_claims.end(),
			[&](const auto& c) { return c.first == a_id.form; });
	}
}
