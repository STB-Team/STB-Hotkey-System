#include "BottomBarHint.h"

#include "Localization.h"
#include "Settings.h"

#include <string>
#include <utility>

namespace HKS::BottomBarHint
{
	namespace
	{
		// SkyUI's button data is { text: <label>, controls: <control descriptor> }. The
		// descriptor may be a named game control or, as here, a raw DX scancode -- the same
		// shape skyui.defines.Input uses for Shift/Tab/Enter, so the panel renders the
		// matching keycap for us and stays correct if the player remaps the modifier.
		bool AddHint(RE::GFxMovie* a_movie, RE::GFxValue& a_panel, const std::string& a_text,
			std::uint32_t a_scancode)
		{
			RE::GFxValue data;
			RE::GFxValue controls;
			RE::GFxValue text;
			a_movie->CreateObject(&data);
			a_movie->CreateObject(&controls);
			a_movie->CreateString(&text, a_text.c_str());
			if (!data.IsObject() || !controls.IsObject()) {
				return false;
			}
			controls.SetMember("keyCode", RE::GFxValue{ static_cast<double>(a_scancode) });
			data.SetMember("text", text);
			data.SetMember("controls", controls);

			RE::GFxValue added;
			if (!a_panel.Invoke("addButton", &added, &data, 1)) {
				return false;
			}
			// addButton returns undefined when the panel has no free button left.
			return added.IsObject();
		}

		class UpdateBottomBarHook : public RE::GFxFunctionHandler
		{
		public:
			explicit UpdateBottomBarHook(RE::GFxValue a_old) :
				_old(std::move(a_old))
			{}

			void Call(Params& a_params) override
			{
				_old.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);

				if (!Settings::ShowAssignHint() || !a_params.thisPtr || !a_params.movie) {
					return;
				}
				// updateBottomBar(bSelected): with nothing highlighted the row shows the
				// menu-wide actions (Exit / Search / ...), where an item hint makes no sense.
				if (a_params.argCount < 1 || !a_params.args[0].IsBool() || !a_params.args[0].GetBool()) {
					return;
				}

				RE::GFxValue panel;
				if (!a_params.thisPtr->GetMember("navPanel", &panel) || !panel.IsObject()) {
					return;
				}

				bool added = AddHint(a_params.movie, panel,
					Localization::Get("$STB_HK_Hint_Assign"), Settings::AssignModifier());
				if (Settings::GroupModifier() != 0) {
					added = AddHint(a_params.movie, panel,
								Localization::Get("$STB_HK_Hint_Group"), Settings::GroupModifier()) ||
					        added;
				}
				if (added) {
					// The panel only lays buttons out on request; `true` means do it now
					// rather than on the next interval, so the row doesn't visibly reflow.
					RE::GFxValue instant{ true };
					panel.Invoke("updateButtons", nullptr, &instant, 1);
				}
			}

		private:
			RE::GFxValue _old;
		};
	}

	void Setup(RE::IMenu* a_menu)
	{
		if (!a_menu || !a_menu->uiMovie) {
			return;
		}

		// Wrapped on the menu OBJECT, not on a class prototype: every SkyUI item menu has
		// its own class (InventoryMenu, MagicMenu, ...) but they all live at _root.Menu_mc,
		// and an own property shadows the inherited method for calls made on this instance.
		// One path covers them all and there is nothing to undo when the menu closes.
		RE::GFxValue menuObj;
		if (!a_menu->uiMovie->GetVariable(&menuObj, "_root.Menu_mc") || !menuObj.IsObject()) {
			return;
		}

		RE::GFxValue oldUpdate;
		if (!menuObj.GetMember("updateBottomBar", &oldUpdate) || !oldUpdate.IsObject()) {
			logger::warn("BottomBarHint: no updateBottomBar on this menu -- hint skipped");
			return;
		}

		auto         impl = RE::make_gptr<UpdateBottomBarHook>(std::move(oldUpdate));
		RE::GFxValue newUpdate;
		a_menu->uiMovie->CreateFunction(&newUpdate, impl.get());
		menuObj.SetMember("updateBottomBar", newUpdate);
		logger::info("BottomBarHint: hooked updateBottomBar");
	}
}
