// Server-side distance for flag actions. CCTCursor trusts the client cursor hit.
class LFPG_ActionGuards
{
    static const float FLAG_ACTION_MAX_DISTANCE = 5.0;

    static bool IsPlayerNearTarget(PlayerBase player, Object targetObj)
    {
        if (!player)
            return false;
        if (!targetObj)
            return false;

        vector playerPos = player.GetPosition();
        vector targetPos = targetObj.GetPosition();
        float maxDist = FLAG_ACTION_MAX_DISTANCE;
        float maxDistSq = maxDist * maxDist;
        float distSq = vector.DistanceSq(playerPos, targetPos);
        if (distSq > maxDistSq)
            return false;

        return true;
    }
};
