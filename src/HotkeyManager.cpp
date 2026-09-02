#include "HotkeyManager.h"

namespace HKS
{
	HotkeyManager* HotkeyManager::GetSingleton()
	{
		static HotkeyManager singleton;
		return &singleton;
	}

	HotkeyManager::AssignResult HotkeyManager::Assign(const Bind& a_bind, const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);

		// Same chord already bound to the same exact item -> toggle off.
		for (auto it = _hotkeys.begin(); it != _hotkeys.end(); ++it) {
			if (it->bind == a_bind && it->id.Same(a_id)) {
				_hotkeys.erase(it);
				return AssignResult::kRemoved;
			}
		}

		const bool replacing =
			std::erase_if(_hotkeys, [&](const Hotkey& h) {
				return h.bind == a_bind || h.id.Same(a_id);
			}) > 0;

		_hotkeys.push_back(Hotkey{ a_bind, a_id });
		return replacing ? AssignResult::kReplaced : AssignResult::kAdded;
	}

	bool HotkeyManager::RemoveByBind(const Bind& a_bind)
	{
		std::scoped_lock lk(_lock);
		return std::erase_if(_hotkeys, [&](const Hotkey& h) { return h.bind == a_bind; }) > 0;
	}

	bool HotkeyManager::RemoveByItem(const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);
		return std::erase_if(_hotkeys, [&](const Hotkey& h) { return h.id.Same(a_id); }) > 0;
	}

	const Hotkey* HotkeyManager::FindByItem(const ItemId& a_id) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.id.Same(a_id)) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::FindByForm(RE::FormID a_form) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.id.form == a_form) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::ResolveChord(
		RE::INPUT_DEVICE                         a_device,
		const std::unordered_set<std::uint32_t>& a_held,
		std::uint32_t                            a_trigger) const
	{
		std::scoped_lock lk(_lock);

		const Hotkey* best = nullptr;
		for (const auto& h : _hotkeys) {
			if (h.bind.device != a_device) {
				continue;
			}
			// The just-pressed key must be part of the chord, so an unrelated held/phantom
			// key can never shadow the bind the user actually triggered.
			if (a_trigger != 0 &&
				std::find(h.bind.keys.begin(), h.bind.keys.end(), a_trigger) == h.bind.keys.end()) {
				continue;
			}
			if (!h.bind.IsSatisfiedBy(a_held)) {
				continue;
			}
			// Prefer the most specific (longest) satisfied chord.
			if (!best || h.bind.keys.size() > best->bind.keys.size()) {
				best = &h;
			}
		}
		return best;
	}

	std::vector<Hotkey> HotkeyManager::Snapshot() const
	{
		std::scoped_lock lk(_lock);
		return _hotkeys;
	}

	void HotkeyManager::ReplaceAll(std::vector<Hotkey> a_hotkeys)
	{
		std::scoped_lock lk(_lock);
		_hotkeys = std::move(a_hotkeys);
	}

	void HotkeyManager::Clear()
	{
		std::scoped_lock lk(_lock);
		_hotkeys.clear();
	}
}
