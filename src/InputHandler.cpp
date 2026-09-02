#include "InputHandler.h"

#include "EquipDispatch.h"
#include "Favorites.h"
#include "HotkeyManager.h"
#include "InventoryIcons.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <vector>

namespace HKS
{
	InputHandler* InputHandler::GetSingleton()
	{
		static InputHandler singleton;
		return &singleton;
	}

	void InputHandler::Register()
	{
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(static_cast<RE::BSTEventSink<RE::InputEvent*>*>(GetSingleton()));
			logger::info("input sink registered");
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			static_cast<RE::BSTEventSource<RE::MenuOpenCloseEvent>*>(ui)->AddEventSink(
				static_cast<RE::BSTEventSink<RE::MenuOpenCloseEvent>*>(GetSingleton()));
			logger::info("menu open/close sink registered");
		}
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		const RE::MenuOpenCloseEvent*              a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_source*/)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}
		// Only resync at the boundaries of menus where a stuck key actually does harm --
		// the Favorites menu (uncloseable on a stuck modifier) and the SkyUI item menus
		// that share the assign path. Transient menus (cursor, tooltips) are ignored so an
		// in-progress capture isn't dropped by an unrelated toggle.
		const std::string_view name{ a_event->menuName.c_str() };
		if (name != RE::FavoritesMenu::MENU_NAME &&
			name != RE::InventoryMenu::MENU_NAME &&
			name != RE::ContainerMenu::MENU_NAME &&
			name != RE::MagicMenu::MENU_NAME &&
			name != RE::GiftMenu::MENU_NAME &&
			name != RE::BarterMenu::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}
		// Drop any stale pressed keys and abandon a half-finished capture so a stuck
		// modifier can never brick the menu.
		_kbHeld.clear();
		_msHeld.clear();
		_padHeld.clear();
		_capturing = false;
		_capChord.clear();
		_capDown.clear();
		_capTarget = {};

		// On open, reconcile bindings with favorite state: a hotkey whose item the player
		// un-favorited (vanilla F) drops its binding + keycap now, instead of lingering and
		// reappearing when the item next returns to the inventory. Done on open only, so it
		// never races the auto-favorite that assignment queues while the menu is open.
		if (a_event->opening && Favorites::PruneUnfavorited()) {
			InventoryIcons::MarkDirty();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	std::unordered_set<std::uint32_t>& InputHandler::HeldFor(RE::INPUT_DEVICE a_device)
	{
		switch (a_device) {
		case RE::INPUT_DEVICE::kMouse:
			return _msHeld;
		case RE::INPUT_DEVICE::kGamepad:
			return _padHeld;
		default:
			return _kbHeld;
		}
	}

	bool InputHandler::IsHeld(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const
	{
		switch (a_device) {
		case RE::INPUT_DEVICE::kMouse:
			return _msHeld.contains(a_key);
		case RE::INPUT_DEVICE::kGamepad:
			return _padHeld.contains(a_key);
		default:
			return _kbHeld.contains(a_key);
		}
	}

	ItemId InputHandler::ResolveForKey(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const
	{
		auto held = const_cast<InputHandler*>(this)->HeldFor(a_device);
		held.insert(a_key);  // the just-pressed key may not be in the set yet
		const auto* hk = HotkeyManager::GetSingleton()->ResolveChord(a_device, held, a_key);
		return hk ? hk->Id() : ItemId{};
	}

	bool InputHandler::FiringSuppressed()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return true;
		}
		// Never equip/unequip while an item menu is showing: doing so frees/moves the
		// ExtraDataList that Inventory3DManager's preview holds -> UAF crash.
		if (MenuAssign::IsAssignMenuOpen()) {
			return true;
		}
		// Menus that own input but don't pause (or are un-paused by SkyrimSoulsRE) and
		// aren't "application" menus -- chiefly the dialogue menu. Enumerated by name so
		// it still holds under Souls, which keeps input flowing. Fader/Loading mirror the
		// transition guard vanilla FavoritesHandler::ProcessButton applies.
		static constexpr std::string_view kBlockingMenus[] = {
			RE::DialogueMenu::MENU_NAME,
			RE::CraftingMenu::MENU_NAME,
			RE::BookMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::SleepWaitMenu::MENU_NAME,
			RE::TrainingMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::LevelUpMenu::MENU_NAME,
			RE::MessageBoxMenu::MENU_NAME,
			RE::Console::MENU_NAME,
			RE::LoadingMenu::MENU_NAME,
			RE::FaderMenu::MENU_NAME,
		};
		for (const auto& name : kBlockingMenus) {
			if (ui->IsMenuOpen(name)) {
				return true;
			}
		}
		// Paused, or an application menu (inventory/magic/map/stats/journal/...) owns the
		// screen. We deliberately do NOT gate on ControlMap::IsMovementControlsEnabled():
		// vanilla favorites don't, and that flag is also cleared by quests/cutscenes that
		// merely disable movement -- states where a vanilla hotkey would still equip. That
		// broad gate was what made hotkeys "randomly" dead when vanilla would have worked.
		return ui->GameIsPaused() || ui->IsApplicationMenuOpen();
	}

	void InputHandler::CommitCapture()
	{
		if (_capTarget && !_capChord.empty()) {
			Bind bind;
			bind.device = _capDevice;
			bind.keys.assign(_capChord.begin(), _capChord.end());
			bind.Canonicalize();
			const auto res = HotkeyManager::GetSingleton()->Assign(bind, _capTarget);
			logger::info("assigned chord ({} keys, dev {}) -> form {:08X} ench {:08X} uid {} hp {} (result {})",
				bind.keys.size(), static_cast<int>(_capDevice), _capTarget.form, _capTarget.ench,
				_capTarget.uid, _capTarget.health, static_cast<int>(res));
			InventoryIcons::MarkDirty();  // re-stamp menu keycaps (handles displaced bindings)
			// Favorite it right away (star + shows in Favorites with our badge), like a
			// vanilla F press. Prefer the game's real-entry path (live refresh); fall back
			// to the by-form path otherwise.
			if (res != HotkeyManager::AssignResult::kRemoved) {
				if (!Favorites::FavoriteSelectedItem(_capTarget.form)) {
					Favorites::EnsureFavorited(_capTarget.form);
				}
			}
		}
		else if (!_capChord.empty()) {
			// Keys were captured but the highlighted entry gave no form -- the selection
			// couldn't be read (menu path mismatch, nothing highlighted, non-item row).
			// Logged because to the player this looks like "the hotkey just didn't take".
			logger::warn("assign aborted: {} key(s) captured but no target form under the cursor",
				_capChord.size());
		}
		// Reset only the pending-chord state; the capture *session* stays open until the
		// modifier is released (handled in ProcessEvent), so the next item can be bound in
		// the same hold without releasing the modifier.
		_capChord.clear();
		_capDown.clear();
		_capTarget = {};
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		RE::InputEvent* const*               a_event,
		RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// The console overlays the assign menu but steals input focus -- never capture a
		// chord (or assign) while it's up; the player is typing commands, not binding.
		auto*               ui = RE::UI::GetSingleton();
		const bool          consoleOpen = ui && ui->IsMenuOpen(RE::Console::MENU_NAME);
		const bool          assignOpen = MenuAssign::IsAssignMenuOpen() && !consoleOpen;
		const std::uint32_t modKey = Settings::AssignModifier();

		if (!assignOpen && _capturing) {  // menu closed mid-capture -> abandon
			_capturing = false;
			_capChord.clear();
			_capDown.clear();
			_capTarget = {};
		}

		for (auto* e = *a_event; e; e = e->next) {
			auto* button = e->AsButtonEvent();
			if (!button || !button->HasIDCode()) {
				continue;
			}

			const auto device = button->device.get();
			const auto idCode = button->GetIDCode();
			const bool pressed = button->IsPressed();
			auto&      held = HeldFor(device);

			if (pressed) {
				held.insert(idCode);
			} else {
				held.erase(idCode);
			}

			// ---- assignment capture (an assign-menu open) ----
			// The assign-modifier is a keyboard key; the chord it captures can be keyboard
			// OR mouse keys (a chord stays single-device, set by its first key).
			if (assignOpen) {
				if (device == RE::INPUT_DEVICE::kKeyboard && idCode == modKey) {
					if (button->IsDown()) {
						// Open a capture session. The target is NOT locked here -- it's
						// locked at each chord's first key-down (below), so between binds
						// the selection can move freely (mouse-hover the next item) and you
						// can bind several items in one modifier hold.
						_capturing = true;
						_capChord.clear();
						_capDown.clear();
						_capTarget = {};
						_capDevice = RE::INPUT_DEVICE::kKeyboard;
					} else if (!pressed && _capturing) {
						// Modifier released -> commit any pending chord and end the session.
						CommitCapture();
						_capturing = false;
					}
					continue;  // the modifier key never fires a hotkey
				}
				if (_capturing && (device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse)) {
					if (button->IsDown()) {
						const std::size_t maxKeys = Settings::EnableChords() ? kMaxChord : 1;
						if (_capChord.empty()) {
							// First key of a new chord: fix its device and lock the item now
							// (before the key can move SkyUI's type-search selection).
							_capDevice = device;
							_capTarget = MenuAssign::GetSelectedAssignTarget();
						}
						if (device == _capDevice && _capChord.size() < maxKeys) {
							_capChord.insert(idCode);
							_capDown.insert(idCode);
							// Chord full -> commit at once, no modifier release needed. Its
							// keys stay down; their key-ups are ignored (CommitCapture
							// cleared _capDown), so they can't re-commit.
							if (_capChord.size() >= maxKeys) {
								CommitCapture();
							}
						}
					} else if (_capDown.erase(idCode) && _capDown.empty() && !_capChord.empty()) {
						// A not-yet-full chord whose keys were all released -> commit as-is.
						// This is what lets a single key commit on release while the modifier
						// is still held, so several single-key binds fire off in one hold.
						CommitCapture();
					}
					continue;  // captured keys must not fire
				}
			}

			// ---- firing ----
			if (!pressed || !button->IsDown() || FiringSuppressed()) {
				continue;  // only the completing down-stroke fires, never while paused
			}

			// ResolveChord only returns a chord that contains idCode, so the matched bind
			// is one this keystroke actually completes (an unrelated held/phantom key can't
			// shadow it and drop the press).
			const auto* hotkey = HotkeyManager::GetSingleton()->ResolveChord(device, held, idCode);
			if (hotkey && hotkey->id.form) {
				// Voice forms (shout/power) are equipped+cast by the ShoutHandler hook,
				// which runs before this sink, so skip them here to avoid a double
				// equip that would interrupt the cast.
				auto* form = RE::TESForm::LookupByID(hotkey->id.form);
				if (Settings::CastVoiceOnEquip() && EquipDispatch::IsVoiceForm(form)) {
					continue;
				}
				EquipDispatch::Fire(hotkey->Id());
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
