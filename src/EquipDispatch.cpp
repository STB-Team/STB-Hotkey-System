#include "EquipDispatch.h"

#include "Favorites.h"
#include "HotkeyManager.h"
#include "Settings.h"

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

		void EquipSpellForm(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em, RE::SpellItem* a_spell)
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

			// One-handed: mirror vanilla favorites, prefer the empty/other hand.
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
		RE::ExtraDataList* FindUnwornInstance(RE::TESBoundObject* a_bound, RE::FormID a_ench, std::int32_t a_health)
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
					if (!xl) {
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
					EquipSpellForm(player, em, spell);
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
					if (dualWield && (inRight != inLeft) &&
						CountMatchingInstances(bound, a_id.ench, a_id.health) >= 2) {
						constexpr RE::FormID kRight = 0x13F42;
						constexpr RE::FormID kLeft = 0x13F43;
						auto* freeXl = FindUnwornInstance(bound, a_id.ench, a_id.health);
						em->EquipObject(player, bound, freeXl, 1, EquipSlot(inRight ? kLeft : kRight));
						break;
					}

					auto* xl = FindInstanceList(bound, a_id);

					// --- Instance-precise bind (enchanted/tempered/unique copy) ---
					// You only ever own the one specific instance, so a plain toggle is
					// correct. Test "worn" on the EXACT instance, not the base form;
					// otherwise pressing the enchanted sword's hotkey while a plain one
					// (same base) is in hand reads as "already worn" and unequips instead
					// of switching.
					if (xl) {
						const bool worn = xl->HasType(RE::ExtraDataType::kWorn) ||
						                  xl->HasType(RE::ExtraDataType::kWornLeft);
						if (worn) {
							em->UnequipObject(player, bound, xl);
						} else {
							em->EquipObject(player, bound, xl);
						}
						break;
					}

					// --- Fungible bind (plain copies of a base form) ---
					// Default toggle: equip if in neither hand, otherwise put away.
					if (inRight || inLeft) {
						em->UnequipObject(player, bound, nullptr);
					} else {
						em->EquipObject(player, bound, nullptr);
					}
				}
				break;
			}
		}
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

	void Fire(const ItemId& a_id)
	{
		if (!a_id) {
			return;
		}
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask([a_id]() {
			const auto state = Favorites::Query(a_id.form);
			if (state == Favorites::State::kUnfavorited) {
				// Player un-favorited it (vanilla F) -> the hotkey is gone.
				HotkeyManager::GetSingleton()->RemoveByItem(a_id);
				logger::info("fire {:08X}: un-favorited -> binding removed", a_id.form);
				return;
			}
			if (state == Favorites::State::kAbsent) {
				// Out of stock (drank the last potion, sold the gear). Keep the binding --
				// PickupWatch restores the star when the item comes back.
				if (Settings::DebugLog()) {
					logger::info("fire {:08X}: none held -> nothing to equip, binding kept", a_id.form);
				}
				return;
			}
			if (Settings::DebugLog()) {
				logger::info("equip fire -> form {:08X} ench {:08X} hp {}",
					a_id.form, a_id.ench, a_id.health);
			}
			EquipForm(a_id);
		});
	}

	bool EquipNow(const ItemId& a_id)
	{
		if (!a_id) {
			return false;
		}
		const auto state = Favorites::Query(a_id.form);
		if (state == Favorites::State::kUnfavorited) {
			HotkeyManager::GetSingleton()->RemoveByItem(a_id);
			logger::info("voice fire {:08X}: un-favorited -> binding removed", a_id.form);
			return false;
		}
		if (state == Favorites::State::kAbsent) {
			// The player no longer has this power/shout (or holds none of the item).
			if (Settings::DebugLog()) {
				logger::info("voice fire {:08X}: player no longer has it -> not cast", a_id.form);
			}
			return false;
		}
		EquipForm(a_id);
		return true;
	}
}
