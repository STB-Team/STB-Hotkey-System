#include "InventoryIcons.h"

#include "BottomBarHint.h"
#include "Favorites.h"
#include "HotkeyManager.h"
#include "Settings.h"
#include "swfhelper/ImportData.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace HKS
{
	namespace
	{
		constexpr const char* kKeycapSwf = "Interface/STB_Keycaps.swf";
		constexpr const char* kKeycapExport = "STBKeycap";
		constexpr const char* kItemListPath = "_root.Menu_mc.inventoryLists.panelContainer.itemList";

		// skyui.defines.Inventory.ICT_ACTIVE_EFFECT. The Magic menu's Active Effects tab
		// feeds the same itemList; those rows carry the source spell's formId, so they
		// would pick up that spell's keycap. Worse, the list is rebuilt every frame as the
		// effect timers tick, which drops our stamp and makes us re-stamp + UpdateList in a
		// loop -- that is the flicker. Skip them outright.
		constexpr std::uint32_t kActiveEffectType = 11;

		std::vector<ImportData::loadReq> g_loadReq;
		std::atomic<bool>                g_dirty{ true };

		// Per-item scaleform callback: fires as each inventory entry is built, with the
		// real InventoryEntryData. We stamp the instance identity (ExtraUniqueID) onto the
		// GFx entry so icon matching can tell two instances of the same base form apart
		// (e.g. enchanted vs plain sword).
		void ScaleformCallback(RE::GFxMovieView*, RE::GFxValue* a_object, RE::InventoryEntryData* a_item)
		{
			if (!a_object || !a_item) {
				return;
			}
			if (Settings::DebugLog()) {
				static bool loggedOnce = false;
				if (!loggedOnce) {
					loggedOnce = true;
					logger::info("scaleform callback firing (instance-id stamping active)");
				}
			}

			const ItemId id = ReadIdentity(a_item);
			if (Settings::DebugLog() && (id.ench != 0 || id.health != 0)) {
				logger::info("instance {:08X}: ench {:08X} hp {}", id.form, id.ench, id.health);
			}
			a_object->SetMember("STBench", RE::GFxValue{ static_cast<double>(id.ench) });
			a_object->SetMember("STBuid", RE::GFxValue{ static_cast<double>(id.uid) });
			a_object->SetMember("STBhealth", RE::GFxValue{ static_cast<double>(id.health) });
		}

		int DisplayRank(std::uint32_t a_sc)
		{
			switch (a_sc) {
			case 0x1D:
			case 0x9D:
				return 0;  // Ctrl
			case 0x38:
			case 0xB8:
				return 1;  // Alt
			case 0x2A:
			case 0x36:
				return 2;  // Shift
			default:
				return 3;
			}
		}

		void ChordScancodes(const ItemId& a_id, std::uint32_t& a_k1, std::uint32_t& a_k2)
		{
			a_k1 = 0;
			a_k2 = 0;
			const auto* hk = HotkeyManager::GetSingleton()->FindByItem(a_id);
			if (!hk) {
				return;
			}
			auto keys = hk->bind.keys;
			std::sort(keys.begin(), keys.end(), [](std::uint32_t a, std::uint32_t b) {
				const int ra = DisplayRank(a);
				const int rb = DisplayRank(b);
				return ra != rb ? ra < rb : a < b;
			});

			// The keycap clip lays out keyboard scancodes directly, mouse buttons at
			// frame +256, and gamepad at +266 (matches vanilla SetHotkeyIcon). Encode the
			// device into the frame number the AS will gotoAndStop().
			const std::uint32_t offset = (hk->bind.device == RE::INPUT_DEVICE::kMouse) ? 256 :
			                             (hk->bind.device == RE::INPUT_DEVICE::kGamepad) ? 266 : 0;
			if (!keys.empty()) {
				a_k1 = keys[0] + offset;
			}
			if (keys.size() > 1) {
				a_k2 = keys[1] + offset;
			}
		}

		void AttachKeycap(RE::GFxValue* a_parent, const char* a_name, std::uint32_t a_scancode,
			std::int32_t a_depth, double a_x, double a_y, double a_scale)
		{
			RE::GFxValue icon;
			a_parent->GetMember(a_name, &icon);

			if (a_scancode == 0) {
				if (icon.IsObject()) {
					icon.SetMember("_visible", RE::GFxValue{ false });
				}
				return;
			}

			if (!icon.IsObject()) {
				a_parent->AttachMovie(&icon, kKeycapExport, a_name, a_depth, nullptr);
				if (!icon.IsObject()) {
					return;
				}
			}

			icon.GotoAndStop(std::to_string(a_scancode).c_str());
			icon.SetMember("_visible", RE::GFxValue{ true });
			icon.SetMember("_xscale", RE::GFxValue{ a_scale });
			icon.SetMember("_yscale", RE::GFxValue{ a_scale });
			icon.SetMember("_x", RE::GFxValue{ a_x });
			icon.SetMember("_y", RE::GFxValue{ a_y });
		}

		// Wraps InventoryListEntry.prototype.formatName: runs the original, then draws the
		// keycaps from the scancodes the AdvanceMovie pass stamped on the entry object.
		// Vanilla icons that may sit between the item name and the right edge. We place
		// our keycaps to the right of whichever of these are currently shown so they
		// never overlap (favorite star, charge/enchant, poison, "best", read, stolen).
		constexpr const char* kVanillaIconsInv[] = {
			"bestIcon", "favoriteIcon", "poisonIcon", "stolenIcon", "enchIcon", "readIcon"
		};
		constexpr const char* kVanillaIconsFav[] = {
			"equipIcon", "mainHandIcon", "offHandIcon"
		};

		bool IsIconShown(RE::GFxValue& a_icon)
		{
			RE::GFxValue v;
			if (a_icon.GetMember("_visible", &v) && v.IsBool() && !v.GetBool()) {
				return false;
			}
			// SkyUI status icons park on frame 1 when empty; >1 means something is drawn.
			if (a_icon.GetMember("_currentframe", &v) && v.IsNumber() && v.GetNumber() <= 1.0) {
				return false;
			}
			return true;
		}

		double RightEdgeOf(RE::GFxValue& a_obj)
		{
			RE::GFxValue x, w;
			const double ix = (a_obj.GetMember("_x", &x) && x.IsNumber()) ? x.GetNumber() : 0.0;
			const double iw = (a_obj.GetMember("_width", &w) && w.IsNumber()) ? w.GetNumber() : 0.0;
			return ix + iw;
		}

		// Draw the keycap(s) for one list row. a_clip is the entry MovieClip, a_data the
		// entry data object (carries the stamped hotkeyKey1/2), a_textField the name field.
		void RenderKeycaps(RE::GFxValue* a_clip, RE::GFxValue& a_data, RE::GFxValue* a_textField,
			Settings::MenuKind a_kind)
		{
			if (!a_clip || !a_data.IsObject()) {
				return;
			}

			std::uint32_t k1 = 0;
			std::uint32_t k2 = 0;
			RE::GFxValue  v;
			if (a_data.GetMember("hotkeyKey1", &v) && v.IsNumber()) {
				k1 = static_cast<std::uint32_t>(v.GetNumber());
			}
			if (a_data.GetMember("hotkeyKey2", &v) && v.IsNumber()) {
				k2 = static_cast<std::uint32_t>(v.GetNumber());
			}

			const double scale = Settings::IconScale();
			const double y = Settings::IconY();

			double x1 = Settings::IconX(a_kind);
			if (Settings::IconAfterName()) {
				double rightEdge = 0.0;

				if (a_textField && a_textField->IsObject()) {
					double       fx = 0.0;
					double       w = 0.0;
					RE::GFxValue fv;
					if (a_textField->GetMember("_x", &fv) && fv.IsNumber()) {
						fx = fv.GetNumber();
					}
					if (a_textField->GetMember("textWidth", &fv) && fv.IsNumber() && fv.GetNumber() > 0.0) {
						w = fv.GetNumber();
					} else if (a_textField->GetMember("_width", &fv) && fv.IsNumber()) {
						w = fv.GetNumber();
					}
					rightEdge = fx + w;
				}

				// Push past any shown vanilla icons so we don't cover them.
				const auto* names = (a_kind == Settings::MenuKind::kFavorites) ? kVanillaIconsFav : kVanillaIconsInv;
				const auto  count = (a_kind == Settings::MenuKind::kFavorites)
				                    ? std::size(kVanillaIconsFav)
				                    : std::size(kVanillaIconsInv);
				for (std::size_t i = 0; i < count; ++i) {
					RE::GFxValue icon;
					if (a_clip->GetMember(names[i], &icon) && icon.IsObject() && IsIconShown(icon)) {
						rightEdge = (std::max)(rightEdge, RightEdgeOf(icon));
					}
				}

				x1 = rightEdge + Settings::IconX(a_kind);  // IconX is the gap after the rightmost icon
			}
			const double x2 = x1 + Settings::IconGap(a_kind);

			AttachKeycap(a_clip, "STBhk1", k1, 0x6FFFFFF0, x1, y, scale);
			AttachKeycap(a_clip, "STBhk2", k2, 0x6FFFFFF1, x2, y, scale);
		}

		// Inventory/Magic/Container/etc: formatName(entryField, entryObject, state).
		class FormatNameHook : public RE::GFxFunctionHandler
		{
		public:
			FormatNameHook(RE::GFxValue a_old, Settings::MenuKind a_kind) :
				_old(std::move(a_old)),
				_kind(a_kind)
			{}

			void Call(Params& a_params) override
			{
				_old.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);
				if (a_params.argCount < 2 || !a_params.thisPtr) {
					return;
				}
				RenderKeycaps(a_params.thisPtr, a_params.args[1], &a_params.args[0], _kind);
			}

		private:
			RE::GFxValue       _old;
			Settings::MenuKind _kind;
		};

		// Favorites: setEntry(entryObject, state) on the entry clip; the name field is
		// the clip's own textField member.
		class SetEntryHook : public RE::GFxFunctionHandler
		{
		public:
			explicit SetEntryHook(RE::GFxValue a_old) :
				_old(std::move(a_old))
			{}

			void Call(Params& a_params) override
			{
				_old.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);
				if (a_params.argCount < 1 || !a_params.thisPtr) {
					return;
				}
				RE::GFxValue textField;
				a_params.thisPtr->GetMember("textField", &textField);
				RenderKeycaps(a_params.thisPtr, a_params.args[0], &textField, Settings::MenuKind::kFavorites);
			}

		private:
			RE::GFxValue _old;
		};

		void InjectKeycaps(RE::IMenu* a_menu)
		{
			ImportData::ImportResources(ImportData::GetMovieDefImpl(a_menu->uiMovie->GetMovieDef()), g_loadReq);
			for (auto& req : g_loadReq) {
				req.resources.clear();
			}
		}

		void Setup(RE::IMenu* a_menu, Settings::MenuKind a_kind, bool a_bottomBarHint)
		{
			if (!a_menu || !a_menu->uiMovie) {
				return;
			}

			Settings::Load();    // re-read so INI tweaks apply on the next menu open
			g_dirty.store(true);  // (re)stamp this menu's entries

			RE::GFxValue proto;
			if (!a_menu->uiMovie->GetVariable(&proto, "_global.InventoryListEntry.prototype") || !proto.IsObject()) {
				logger::warn("InventoryIcons: no InventoryListEntry prototype");
				return;
			}

			RE::GFxValue oldFormatName;
			proto.GetMember("formatName", &oldFormatName);

			auto impl = RE::make_gptr<FormatNameHook>(std::move(oldFormatName), a_kind);
			RE::GFxValue newFormatName;
			a_menu->uiMovie->CreateFunction(&newFormatName, impl.get());
			proto.SetMember("formatName", newFormatName);

			InjectKeycaps(a_menu);

			// Only where binding is the natural thing to do. Container/Barter/Gift can
			// assign too, but their button row is about moving items between two sides and
			// a hotkey hint there is just noise.
			if (a_bottomBarHint) {
				BottomBarHint::SetupItemMenu(a_menu);
			}
			logger::info("InventoryIcons: hooked formatName + injected keycaps");
		}

		void SetupFavorites(RE::IMenu* a_menu)
		{
			if (!a_menu || !a_menu->uiMovie) {
				return;
			}

			Settings::Load();

			RE::GFxValue proto;
			if (!a_menu->uiMovie->GetVariable(&proto, "_global.FavoritesListEntry.prototype") || !proto.IsObject()) {
				logger::warn("InventoryIcons: no FavoritesListEntry prototype");
				return;
			}

			RE::GFxValue oldSetEntry;
			proto.GetMember("setEntry", &oldSetEntry);

			auto impl = RE::make_gptr<SetEntryHook>(std::move(oldSetEntry));
			RE::GFxValue newSetEntry;
			a_menu->uiMovie->CreateFunction(&newSetEntry, impl.get());
			proto.SetMember("setEntry", newSetEntry);

			InjectKeycaps(a_menu);
			// Untarnished-style favorites menus draw their own hint rows; a vanilla one
			// draws none and this does nothing.
			BottomBarHint::SetupFavorites(a_menu);
			logger::info("InventoryIcons: hooked favorites setEntry + injected keycaps");
		}

		// Diff-stamp each entry's chord scancodes by FormID; re-render only on change.
		// Throttled rather than size-gated: enchanting/tempering can rebuild an entry
		// object (dropping our stamp) without changing the list length, so we re-scan
		// periodically and let the per-entry diff suppress redundant UpdateList calls.
		void PushKeycaps(RE::IMenu* a_menu, bool a_livePrune)
		{
			static int frame = 0;

			if (!a_menu || !a_menu->uiMovie) {
				return;
			}
			const bool forced = g_dirty.exchange(false);
			if (!forced && (++frame % 15 != 0)) {
				return;
			}

			// Reconcile bindings with favorite state on the same throttle tick, so a
			// vanilla F un-favorite drops the binding (and its keycap) while the menu stays
			// open. The stamping loop below then reads no chord for that item and clears
			// the keycap by itself. Only for menus where un-favoriting is the natural
			// action -- in Container/Barter/Gift an item moved to the other side reads as
			// "not favorited" while the trade is still pending, which must not drop it.
			if (a_livePrune) {
				Favorites::PruneUnfavorited();
			}

			RE::GFxValue itemList;
			if (!a_menu->uiMovie->GetVariable(&itemList, kItemListPath) || !itemList.IsObject()) {
				return;
			}
			RE::GFxValue entryList;
			if (!itemList.GetMember("_entryList", &entryList) || !entryList.IsArray()) {
				return;
			}

			const auto size = entryList.GetArraySize();
			bool       changed = false;
			for (std::uint32_t i = 0; i < size; ++i) {
				RE::GFxValue entry;
				if (!entryList.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}

				const auto readNum = [&](const char* a_name) -> std::uint32_t {
					RE::GFxValue v;
					return (entry.GetMember(a_name, &v) && v.IsNumber()) ? static_cast<std::uint32_t>(v.GetNumber()) : 0;
				};

				std::uint32_t k1 = 0;
				std::uint32_t k2 = 0;
				// In Barter/Container the same itemList shows BOTH sides. SkyUI tags the
				// player's own items with the inventory filter flags (< 1024) and the
				// merchant/container stock with the container flags (>= 2048). A fungible
				// bind (plain potion, all-zero identity) would otherwise match a merchant
				// item of the same base form and paint a keycap on the vendor's goods.
				// Only the player can have a hotkey, so skip the non-player side entirely.
				if (readNum("type") == kActiveEffectType) {
					continue;  // active effect row -- never ours to stamp
				}

				const bool playerSide = readNum("filterFlag") < 1024;
				if (playerSide) {
					if (const auto fid = static_cast<RE::FormID>(readNum("formId"))) {
						ItemId id;
						id.form = fid;
						id.ench = static_cast<RE::FormID>(readNum("STBench"));
						id.uid = static_cast<std::uint16_t>(readNum("STBuid"));
						id.health = static_cast<std::int32_t>(readNum("STBhealth"));
						ChordScancodes(id, k1, k2);
					}
				}
				if (readNum("hotkeyKey1") == k1 && readNum("hotkeyKey2") == k2) {
					continue;
				}

				entry.SetMember("hotkeyKey1", RE::GFxValue{ static_cast<double>(k1) });
				entry.SetMember("hotkeyKey2", RE::GFxValue{ static_cast<double>(k2) });
				changed = true;
			}

			if (changed) {
				itemList.Invoke("UpdateList");
			}
		}

		// --- per-menu vtable thunks ------------------------------------------------
#define HKS_MENU_HOOKS(TAG, KIND, LIVEPRUNE, HINT)                                                       \
	REL::Relocation<void (*)(RE::IMenu*)>                       _pc##TAG;                          \
	REL::Relocation<void (*)(RE::IMenu*, float, std::uint32_t)> _adv##TAG;                         \
	void PostCreate##TAG(RE::IMenu* a_this)                                                        \
	{                                                                                             \
		Setup(a_this, KIND, HINT);                                                                       \
		_pc##TAG(a_this);                                                                         \
	}                                                                                             \
	void Advance##TAG(RE::IMenu* a_this, float a_interval, std::uint32_t a_time)                   \
	{                                                                                             \
		_adv##TAG(a_this, a_interval, a_time);                                                    \
		PushKeycaps(a_this, LIVEPRUNE);                                                           \
	}

		// Live prune only where the player un-favorites (F); trade menus move items
		// between sides, which would read as un-favorited mid-transaction.
		HKS_MENU_HOOKS(Inv, Settings::MenuKind::kInventory, true, true)
		HKS_MENU_HOOKS(Cont, Settings::MenuKind::kInventory, false, false)
		HKS_MENU_HOOKS(Magic, Settings::MenuKind::kMagic, true, true)
		HKS_MENU_HOOKS(Gift, Settings::MenuKind::kInventory, false, false)
		HKS_MENU_HOOKS(Bart, Settings::MenuKind::kInventory, false, false)
#undef HKS_MENU_HOOKS

		// Favorites: only the PostCreate (inject + setEntry hook) lives here; the
		// scancode stamping is done by FavoritesHook::PushBadges on AdvanceMovie.
		REL::Relocation<void (*)(RE::IMenu*)> _pcFav;
		void                                  PostCreateFav(RE::IMenu* a_this)
		{
			SetupFavorites(a_this);
			_pcFav(a_this);
		}
	}

	void InventoryIcons::MarkDirty()
	{
		g_dirty.store(true);
	}

	void InventoryIcons::LoadResources()
	{
		g_loadReq.clear();
		g_loadReq.push_back(ImportData::loadReq{ .sourcePath = kKeycapSwf, .exports = { kKeycapExport }, .resources = {} });
	}

	void InventoryIcons::Install()
	{
#define HKS_INSTALL(TAG, VTBL)                                          \
	{                                                                  \
		REL::Relocation<std::uintptr_t> v{ RE::VTBL[0] };             \
		_pc##TAG = v.write_vfunc(0x2, &PostCreate##TAG);              \
		_adv##TAG = v.write_vfunc(0x5, &Advance##TAG);                \
	}

		HKS_INSTALL(Inv, VTABLE_InventoryMenu)
		HKS_INSTALL(Cont, VTABLE_ContainerMenu)
		HKS_INSTALL(Magic, VTABLE_MagicMenu)
		HKS_INSTALL(Gift, VTABLE_GiftMenu)
		HKS_INSTALL(Bart, VTABLE_BarterMenu)
#undef HKS_INSTALL

		// Favorites: PostCreate only (FavoritesHook owns AdvanceMovie).
		{
			REL::Relocation<std::uintptr_t> v{ RE::VTABLE_FavoritesMenu[0] };
			_pcFav = v.write_vfunc(0x2, &PostCreateFav);
		}

		// Per-item callback that stamps instance uid onto inventory entries.
		SKSE::GetScaleformInterface()->Register(ScaleformCallback);

		logger::info("InventoryIcons installed");
	}
}
