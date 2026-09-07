#pragma once

#include <cstdint>

namespace sunrise::middleware::content::packages::tables {

/** Engine slot kinds, named by their own import-interface vtables. */
enum class SlotType : std::uint16_t {
    squadSensor = 1,
    combatantSensor = 2,
    objectiveSensor = 3,
    objectSensor = 4,
    sequenceSensor = 5,
    cinematicSensor = 6,
    hudSensor = 8,
    allPlayersVolumeSensor = 10,
    musicSensor = 11,
    musicSectionProxy = 12,
    playerSpasahaSensor = 13,
    activityTeamSensor = 14,
    teamPickerSensor = 15,
    scoreboardSensor = 16,
    activityLifetimeSensor = 17,
    activityTimerSensor = 18,
    playerNavigationSensor = 19,
    damageComponentSensor = 20,
    distanceSensor = 21,
    objectVolumeSensor = 22,
    deviceSensor = 23,
    channelSensor = 24,
    lookTriggerSensor = 25,
    hopOnSensor = 26,
    raceTrackSensor = 27,
    hardWipeSensor = 28,
    objectMonitorSensor = 29,
    playerMonitorSensor = 30,
    playerTriggerSensor = 31,
    toggleSensor = 32,
    playerObjectiveSensor = 33,
    objectFilterSensor = 34,
    activityHardWipeGlobalsSensor = 35,
    lootSensor = 36,
    mapGeneratorSensor = 37,
    taskSensor = 38,
    passengerSensor = 39,
    targetSensor = 40,
    newUserExperienceSensor = 41,
    performanceSensor = 42,
    sceneSensor = 43,
    firingArea = 44,
    firingAreaSet = 45,
    squadGroup = 46,
    activityPoint = 47,
    aiPointSet = 48,
    landingPoint = 49,
    activitySpawnPoint = 50,
    activityTeleportPoint = 51,
    spawnInfluencer = 52,
    dialogSensor = 53,
    dialogEventProxy = 54,
    bubbleReference = 57,
    commandScript = 58,
    triggerVolume = 60,
    utilityScript = 61,
    phase = 62,
    slotReference = 63,
    squadInspector = 64,
    ghostLinkSensor = 65,
    spawnRule = 66,
    teamSideSensor = 67,
    directiveSensor = 68,
    directiveProxy = 69,
    encounterEngagementSensor = 70,
    publicEventSensor = 71,
    encounterFlag = 72,
};

/** @return The engine name for a slot type, or "unknown" when the donor has no class. */
[[nodiscard]] constexpr const char* slot_type_name(std::uint16_t type) noexcept {
    switch (static_cast<SlotType>(type)) {
    case SlotType::squadSensor:
        return "squad sensor";
    case SlotType::combatantSensor:
        return "combatant sensor";
    case SlotType::objectiveSensor:
        return "objective sensor";
    case SlotType::objectSensor:
        return "object sensor";
    case SlotType::sequenceSensor:
        return "sequence sensor";
    case SlotType::cinematicSensor:
        return "cinematic sensor";
    case SlotType::hudSensor:
        return "HUD sensor";
    case SlotType::allPlayersVolumeSensor:
        return "all-players volume";
    case SlotType::musicSensor:
        return "music sensor";
    case SlotType::musicSectionProxy:
        return "music section";
    case SlotType::playerSpasahaSensor:
        return "player SPASAHA sensor";
    case SlotType::activityTeamSensor:
        return "activity team";
    case SlotType::teamPickerSensor:
        return "team picker";
    case SlotType::scoreboardSensor:
        return "scoreboard";
    case SlotType::activityLifetimeSensor:
        return "activity lifetime";
    case SlotType::activityTimerSensor:
        return "activity timer";
    case SlotType::playerNavigationSensor:
        return "player navigation";
    case SlotType::damageComponentSensor:
        return "damage component";
    case SlotType::distanceSensor:
        return "distance sensor";
    case SlotType::objectVolumeSensor:
        return "object volume";
    case SlotType::deviceSensor:
        return "device sensor";
    case SlotType::channelSensor:
        return "channel sensor";
    case SlotType::lookTriggerSensor:
        return "look trigger";
    case SlotType::hopOnSensor:
        return "hop-on sensor";
    case SlotType::raceTrackSensor:
        return "race track";
    case SlotType::hardWipeSensor:
        return "hard wipe";
    case SlotType::objectMonitorSensor:
        return "object monitor";
    case SlotType::playerMonitorSensor:
        return "player monitor";
    case SlotType::playerTriggerSensor:
        return "player trigger";
    case SlotType::toggleSensor:
        return "toggle sensor";
    case SlotType::playerObjectiveSensor:
        return "player objective";
    case SlotType::objectFilterSensor:
        return "object filter";
    case SlotType::activityHardWipeGlobalsSensor:
        return "activity hard-wipe globals";
    case SlotType::lootSensor:
        return "loot sensor";
    case SlotType::mapGeneratorSensor:
        return "map generator";
    case SlotType::taskSensor:
        return "task sensor";
    case SlotType::passengerSensor:
        return "passenger sensor";
    case SlotType::targetSensor:
        return "target sensor";
    case SlotType::newUserExperienceSensor:
        return "new-user-experience sensor";
    case SlotType::performanceSensor:
        return "performance sensor";
    case SlotType::sceneSensor:
        return "scene sensor";
    case SlotType::firingArea:
        return "firing area";
    case SlotType::firingAreaSet:
        return "firing-area set";
    case SlotType::squadGroup:
        return "squad group";
    case SlotType::activityPoint:
        return "activity point";
    case SlotType::aiPointSet:
        return "AI point set";
    case SlotType::landingPoint:
        return "landing point";
    case SlotType::activitySpawnPoint:
        return "activity spawn point";
    case SlotType::activityTeleportPoint:
        return "activity teleport point";
    case SlotType::spawnInfluencer:
        return "spawn influencer";
    case SlotType::dialogSensor:
        return "dialog sensor";
    case SlotType::dialogEventProxy:
        return "dialog event";
    case SlotType::bubbleReference:
        return "bubble reference";
    case SlotType::commandScript:
        return "command script";
    case SlotType::triggerVolume:
        return "trigger volume";
    case SlotType::utilityScript:
        return "utility script";
    case SlotType::phase:
        return "phase";
    case SlotType::slotReference:
        return "slot reference";
    case SlotType::squadInspector:
        return "squad inspector";
    case SlotType::ghostLinkSensor:
        return "ghost-link sensor";
    case SlotType::spawnRule:
        return "spawn rule";
    case SlotType::teamSideSensor:
        return "team-side sensor";
    case SlotType::directiveSensor:
        return "directive sensor";
    case SlotType::directiveProxy:
        return "directive";
    case SlotType::encounterEngagementSensor:
        return "encounter engagement";
    case SlotType::publicEventSensor:
        return "public event";
    case SlotType::encounterFlag:
        return "encounter flag";
    }
    return "unknown";
}

} // namespace sunrise::middleware::content::packages::tables
