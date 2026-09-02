class FavoritesListEntry extends skyui.components.list.BasicListEntry
{
   var _alpha;
   var equipIcon;
   var hotkeyIcon;
   var itemIcon;
   var mainHandIcon;
   var offHandIcon;
   var selectIndicator;
   var textField;
   static var STATES = ["None","Equipped","LeftEquip","RightEquip","LeftAndRightEquip"];
   function FavoritesListEntry()
   {
      super();
   }
   function initialize(a_index, a_state)
   {
      super.initialize();
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc4_ = a_entryObject == a_state.assignedEntry;
      var _loc5_ = a_entryObject == a_state.list.selectedEntry || _loc4_;
      var _loc6_ = a_state.activeGroupIndex;
      var _loc7_ = _loc6_ != -1 && (a_entryObject.mainHandFlag & 1 << _loc6_) != 0;
      var _loc8_ = _loc6_ != -1 && (a_entryObject.offHandFlag & 1 << _loc6_) != 0;
      this.isEnabled = a_state.assignedEntry == null || _loc4_;
      this._alpha = !this.isEnabled ? 25 : 100;
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc5_;
      }
      var _loc9_;
      var _loc10_;
      if(a_entryObject.text == undefined)
      {
         this.textField.SetText(" ");
      }
      else if(FavoritesMenu.EXTENDED != 0)
      {
         this.textField.SetText(a_entryObject.text);
         _loc9_ = 40;
         if(this.textField.text.length > _loc9_)
         {
            this.textField.SetText(this.textField.text.substr(0,_loc9_ - 3) + "...");
         }
      }
      else
      {
         _loc10_ = a_entryObject.hotkey;
         if(_loc10_ != undefined && _loc10_ != -1)
         {
            if(_loc10_ >= 0 && _loc10_ <= 7)
            {
               this.textField.SetText(a_entryObject.text);
               this.hotkeyIcon._x = 225;
               this.hotkeyIcon._visible = true;
               this.hotkeyIcon.gotoAndStop(_loc10_ + 2);
            }
            else
            {
               this.textField.SetText("$HK" + _loc10_);
               this.textField.SetText(this.textField.text + ". " + a_entryObject.text);
               this.hotkeyIcon._visible = false;
            }
         }
         else
         {
            this.textField.SetText(a_entryObject.text);
            this.hotkeyIcon._visible = false;
         }
         _loc9_ = 40;
         if(this.textField.text.length > _loc9_)
         {
            this.textField.SetText(this.textField.text.substr(0,_loc9_ - 3) + "...");
         }
      }
      var _loc11_ = a_entryObject.iconLabel == undefined ? "default_misc" : a_entryObject.iconLabel;
      this.itemIcon.gotoAndStop(_loc11_);
      this.itemIcon._alpha = _loc5_ ? 95 : 100;
      if(a_entryObject == null)
      {
         this.equipIcon.gotoAndStop("None");
      }
      else
      {
         this.equipIcon.gotoAndStop(FavoritesListEntry.STATES[a_entryObject.equipState]);
      }
      var _loc12_ = this.textField._x + this.textField.textWidth + 8;
      if(_loc7_)
      {
         this.mainHandIcon._x = _loc12_;
         _loc12_ += 12;
      }
      this.mainHandIcon._visible = _loc7_;
      if(_loc8_)
      {
         this.offHandIcon._x = _loc12_;
      }
      this.offHandIcon._visible = _loc8_;
      this.updateHotkeyIcons(a_entryObject);
   }
   function updateHotkeyIcons(a_entryObject)
   {
      var _loc2_ = a_entryObject.hotkeyKey1;
      var _loc3_ = a_entryObject.hotkeyKey2;
      if(this.hkIcon2 == undefined && this.hotkeyIcon != undefined)
      {
         this.hotkeyIcon.duplicateMovieClip("hkIcon2",this.getNextHighestDepth());
      }
      if(_loc2_ == undefined || _loc2_ == 0)
      {
         if(this.hotkeyIcon != undefined)
         {
            this.hotkeyIcon._visible = false;
         }
         if(this.hkIcon2 != undefined)
         {
            this.hkIcon2._visible = false;
         }
         return undefined;
      }
      this.hotkeyIcon._visible = true;
      this.hotkeyIcon._x = 225;
      this.hotkeyIcon.gotoAndStop(_loc2_);
      if(_loc3_ != undefined && _loc3_ != 0)
      {
         this.hkIcon2._visible = true;
         this.hkIcon2._x = 225 + this.hotkeyIcon._width + 2;
         this.hkIcon2.gotoAndStop(_loc3_);
      }
      else if(this.hkIcon2 != undefined)
      {
         this.hkIcon2._visible = false;
      }
   }
}
