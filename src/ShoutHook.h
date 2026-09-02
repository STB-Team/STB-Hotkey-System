#pragma once

namespace HKS
{
	// Hooks ShoutHandler::CanProcess so a hotkey bound to a shout/power both equips it
	// and immediately fires it on a single press. The hook runs as part of the vanilla
	// player-input handlers -- i.e. before our global input sink -- so we can equip the
	// voice form synchronously here and then let the vanilla ProcessButton cast the
	// now-equipped shout/power.
	class ShoutHook
	{
	public:
		static void Install();

	private:
		static bool CanProcess(RE::ShoutHandler* a_this, RE::InputEvent* a_event);
		static inline REL::Relocation<decltype(CanProcess)> _CanProcess;
	};
}
