#pragma once

#include "Hotkey.h"

#include <mutex>

namespace HKS
{
	// Owns the live set of hotkeys. Pure data + lookups -- it never reads or mutates
	// game inventory/extra-data (the reference mod's UpdateHotkeys() did, and erased
	// valid binds whenever an inventory snapshot didn't line up). The only source of
	// truth is the chord -> FormID table held here and mirrored into the co-save.
	class HotkeyManager
	{
	public:
		enum class AssignResult
		{
			kAdded,     // new chord bound to the form
			kReplaced,  // form/chord re-pointed
			kRemoved,   // same chord+form pressed again -> toggled off
		};

		static HotkeyManager* GetSingleton();

		// Assign an item identity to `a_bind`. Toggle semantics: bind already points at
		// this exact item -> remove it (kRemoved); otherwise drop any hotkey on this bind
		// AND any hotkey on this item, then add it (kAdded / kReplaced). One chord <-> one
		// item.
		AssignResult Assign(const Bind& a_bind, const ItemId& a_id);

		bool RemoveByBind(const Bind& a_bind);
		bool RemoveByItem(const ItemId& a_id);

		// Exact identity lookup (form + ench + uid + health all equal).
		[[nodiscard]] const Hotkey* FindByItem(const ItemId& a_id) const;

		// Match by base form only (any instance). Used where the list only ever shows the
		// bound instance anyway (Favorites menu), so instance data isn't needed.
		[[nodiscard]] const Hotkey* FindByForm(RE::FormID a_form) const;

		// Longest-match resolution: among hotkeys whose device matches and whose whole
		// chord is currently held, return the one with the most keys (so "G" never
		// fires when "Alt+G" was pressed). Returns nullptr if nothing matches.
		//
		// a_trigger (0 = no constraint) is the just-pressed key. When set, only chords
		// that CONTAIN it are considered -- this is essential: otherwise an unrelated key
		// left in the held set (another held hotkey, or a phantom from a missed key-up on
		// alt-tab) can win resolution, and since the trigger isn't in that chord the press
		// is silently dropped. Filtering by the trigger guarantees the matched chord is
		// one the pressed key actually completes.
		[[nodiscard]] const Hotkey* ResolveChord(
			RE::INPUT_DEVICE                            a_device,
			const std::unordered_set<std::uint32_t>&    a_held,
			std::uint32_t                               a_trigger = 0) const;

		[[nodiscard]] const std::vector<Hotkey>& GetAll() const { return _hotkeys; }

		// Locked copy, safe to iterate off-lock (the input sink and the menu/main threads
		// both mutate the store). Used by the un-favorite reconciliation.
		[[nodiscard]] std::vector<Hotkey> Snapshot() const;

		// Bulk replace (load path). Caller has already validated/resolved forms.
		void ReplaceAll(std::vector<Hotkey> a_hotkeys);
		void Clear();

	private:
		HotkeyManager() = default;
		HotkeyManager(const HotkeyManager&) = delete;
		HotkeyManager& operator=(const HotkeyManager&) = delete;

		mutable std::recursive_mutex _lock;
		std::vector<Hotkey>          _hotkeys;
	};
}
