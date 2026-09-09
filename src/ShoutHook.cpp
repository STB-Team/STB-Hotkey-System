#include "ShoutHook.h"

#include "EquipDispatch.h"
#include "InputHandler.h"
#include "Settings.h"

namespace HKS
{
	namespace
	{
		// The voice hotkey key currently "held as a shout button". Once a press is
		// recognised we pass its whole lifecycle (down -> held -> up) to the vanilla
		// ShoutHandler, so it charges the shout by how long the key is held and releases
		// it on key-up -- exactly like pressing the Shout key. Equip happens once, on the
		// initial down.
		bool            g_active = false;
		std::uint32_t   g_key = 0;
		RE::INPUT_DEVICE g_device = RE::INPUT_DEVICE::kKeyboard;
	}

	bool ShoutHook::CanProcess(RE::ShoutHandler* a_this, RE::InputEvent* a_event)
	{
		if (Settings::CastVoiceOnEquip() && a_event) {
			if (auto* button = a_event->AsButtonEvent(); button && button->HasIDCode()) {
				const auto device = button->device.get();
				const auto id = button->GetIDCode();

				// Continuation of an in-progress voice press: keep feeding it to the
				// handler until the key is released.
				if (g_active && device == g_device && id == g_key) {
					if (!button->IsPressed()) {
						g_active = false;  // released -> hand the up event over, then stop
					}
					return true;
				}

				// Fresh press: is it a hotkey bound to a shout/power?
				// Respect the same menu/pause suppression as item hotkeys -- otherwise a
				// voice bind fires straight through the ShoutHandler in dialogue and other
				// menus (vanilla would have refused; our hook runs before that check).
				if (button->IsDown() && !InputHandler::FiringSuppressed()) {
					const auto target = InputHandler::GetSingleton()->ResolveVoiceForKey(device, id);
					if (target) {
						// Only claim the press if something was actually equipped. A power the
						// player has lost (perk/quest removed it) equips nothing -- claiming the
						// key anyway would hand the press to the ShoutHandler, which would then
						// charge and fire whatever is STILL sitting in the voice slot.
						if (EquipDispatch::EquipNow(target)) {  // synchronous; main thread
							g_active = true;
							g_key = id;
							g_device = device;
							return true;  // let ProcessButton begin charging the new shout
						}
						return _CanProcess(a_this, a_event);  // nothing to cast -> vanilla decides
					}
				}
			}
		}
		return _CanProcess(a_this, a_event);
	}

	void ShoutHook::Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ShoutHandler[0] };
		_CanProcess = vtbl.write_vfunc(0x1, &CanProcess);
		logger::info("ShoutHandler::CanProcess hooked (equip+cast voice forms)");
	}
}
