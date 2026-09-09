#include "MenuInputBlock.h"

#include "InputHandler.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <utility>
#include <vector>

namespace HKS
{
	namespace
	{
		// Mouse ids 0..7 are physical buttons; 8/9 are the wheel. Keep the wheel so the
		// list can still be scrolled with the modifier down.
		constexpr std::uint32_t kFirstWheelId = 8;

		// Should this event be hidden from the menus for the duration of the assignment?
		bool Withhold(RE::InputEvent* a_event, const RE::UserEvents* a_ue)
		{
			// SkyUI's type-search runs off char events, not button events -- letting those
			// through would still jump the selection out from under the locked target.
			if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
				return true;
			}
			if (a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
				return false;  // mouse move, thumbstick, device connect -- harmless
			}
			auto* button = a_event->AsButtonEvent();
			if (!button) {
				return false;
			}

			switch (button->device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				// The cancel control is never withheld: the menu has to stay closeable even
				// if the modifier is somehow seen as stuck (the bug that once made the
				// Favorites menu impossible to leave).
				return !(a_ue && button->userEvent == a_ue->cancel);
			case RE::INPUT_DEVICE::kMouse:
				return button->GetIDCode() < kFirstWheelId;
			default:
				return false;  // gamepad: the menus need it to navigate at all
			}
		}
	}

	bool MenuInputBlock::Blocking()
	{
		if (!Settings::BlockMenuKeys() || !MenuAssign::IsAssignMenuOpen()) {
			return false;
		}
		auto* ui = RE::UI::GetSingleton();
		// The console and our own prompts sit on top of the menu and own the keyboard --
		// the player is typing or answering, not binding (this mirrors the capture gate in
		// InputHandler::ProcessEvent).
		if (!ui || ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
			return false;
		}
		auto* input = InputHandler::GetSingleton();
		return input->IsHeld(RE::INPUT_DEVICE::kKeyboard, Settings::AssignModifier()) ||
		       (Settings::GroupModifier() != 0 &&
			       input->IsHeld(RE::INPUT_DEVICE::kKeyboard, Settings::GroupModifier()));
	}

	RE::BSEventNotifyControl MenuInputBlock::ProcessEvent(
		RE::MenuControls*                    a_this,
		RE::InputEvent* const*               a_event,
		RE::BSTEventSource<RE::InputEvent*>* a_source)
	{
		if (!a_event || !*a_event || !Blocking()) {
			return _ProcessEvent(a_this, a_event, a_source);
		}

		const auto* ue = RE::UserEvents::GetSingleton();

		// Relink the chain around the withheld events, run the original over what is left,
		// then put every `next` back. The chain belongs to the input manager and is walked
		// again by the sinks after us, so it must come out of here exactly as it went in.
		std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> saved;
		RE::InputEvent*                                          head = nullptr;
		RE::InputEvent*                                          tail = nullptr;
		for (auto* e = *a_event; e; e = e->next) {
			saved.emplace_back(e, e->next);
			if (Withhold(e, ue)) {
				continue;
			}
			(tail ? tail->next : head) = e;
			tail = e;
		}
		if (tail) {
			tail->next = nullptr;
		}

		// head may be null (everything withheld); vanilla handles an empty chain -- it just
		// runs its own prologue/epilogue, which it would have run anyway.
		const auto result = _ProcessEvent(a_this, &head, a_source);

		for (auto& [node, next] : saved) {
			node->next = next;
		}
		return result;
	}

	void MenuInputBlock::Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MenuControls[0] };  // BSTEventSink<InputEvent*>
		_ProcessEvent = vtbl.write_vfunc(0x1, &MenuInputBlock::ProcessEvent);
		logger::info("MenuControls::ProcessEvent hooked (menu keys withheld while assigning)");
	}
}
