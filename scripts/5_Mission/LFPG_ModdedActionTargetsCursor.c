// ============================================================================
// LFPG_ModdedActionTargetsCursor.c - 5_Mission
// Persistence icon on vehicles protected by a group flag.
//
// Vanilla shows "item_flag_icon" next to the target name inside the range of a
// vanilla territory flag. A vehicle that holds a protected slot of a raised
// group flag shows it as well, also while the cursor is on one of its parts.
// ============================================================================

modded class ActionTargetsCursor
{
    override protected void CheckRefresherFlagVisibilityEx(ActionTarget target)
    {
        super.CheckRefresherFlagVisibilityEx(target);

        if (!target)
            return;
        EntityAI entity = EntityAI.Cast(target.GetObject());
        if (!entity)
            entity = EntityAI.Cast(target.GetParent());
        if (!entity)
            return;

        Transport vehicle = Transport.Cast(entity.GetHierarchyRoot());
        if (!vehicle || !vehicle.LFPG_IsProtected() || vehicle.IsRuined())
            return;

        Widget flagIcon = m_Root.FindAnyWidget("item_flag_icon");
        if (flagIcon)
            flagIcon.Show(true);
    }
};
