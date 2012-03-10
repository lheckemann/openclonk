/*-- Flagpole --*/

#include Library_Flag
#include Library_GoldSeller
#include Library_Base // Needed for DuBuy...

local Name = "$Name$";
local Description = "$Description$";
 
protected func Initialize()
{
	// SetCategory(C4D_StaticBack);
	return _inherited(...);
}

protected func Construction()
{
	SetProperty("MeshTransformation", Trans_Translate(0,4000,0));
	return _inherited(...);
}


/*-- Interaction --*/

public func IsInteractable(object clonk)
	{
	if (!ObjectCount(Find_ID(Rule_BuyAtFlagpole))) return false;
	if (GetCon() < 100) return false;
	return !Hostile(GetOwner(), clonk->GetOwner());
	}

public func GetInteractionMetaInfo(object clonk)
{
	return { Description = "$MsgBuy$", IconName = nil, IconID = Library_Base };
}

public func Interact(object clonk)
{
	var menu;
	var i=0,item,player=clonk->GetOwner(), amount;
	while (item = GetHomebaseMaterial(player, nil, i++))
	{
		amount = GetHomebaseMaterial(player, item);
		// Add even if amount==0
		if (!menu) menu = clonk->CreateRingMenu(Flagpole, this);
		if (!menu) return false;
		menu->AddItem(item, amount, nil);
	}
	if (!menu) return false;
	menu->Show();
	return true;
}

public func Selected(object menu, proplist def, bool alt)
{
	// Safety
	var clonk = menu->GetMenuObject();
	if (!clonk || !IsInteractable(clonk)) return;
	def = def->GetSymbol();
	if (!def) return;
	// Buy
	var item = DoBuy(def, clonk->GetController(), GetOwner(), clonk, alt);
	// Excess objects exit flag (can't get them out...)
	var i = ContentsCount();
	var obj;
	while (i--) if (obj = Contents(i)) Contents(i)->Exit();
	return true;
}
