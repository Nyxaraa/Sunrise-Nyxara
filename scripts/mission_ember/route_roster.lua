-- Every later authored squad, exactly once. SDK task-group bounds were read from package descriptors.
-- Groups are reconstructed progression policy; placements and native AI come from the SDK.
return {
    {name = "processing_entry", region = 56, objective = "EMBER_LINK_PROCESSING_ENTRY_OBJECTIVE", task_groups = 5, squads = {"PROCESSING_ENTRY_SUPPORT_A_SQUAD", "PROCESSING_ENTRY_SUPPORT_B_SQUAD", "PROCESSING_ENTRY_SUPPORT_C_SQUAD", "PROCESSING_ENTRY_SUPPORT_D_SQUAD"}},
    {name = "processing_wave1", region = 56, objective = "EMBER_LINK_PROCESSING_DEFEND_OBJECTIVE", task_groups = 14, squads = {"PROCESSING_DEFEND_DEFENSE_A_SQUAD", "PROCESSING_DEFEND_DEFENSE_B_SQUAD", "PROCESSING_DEFEND_MELEE_A_SQUAD", "PROCESSING_DEFEND_SUPPORT_A_SQUAD"}},
    {name = "processing_wave2", region = 56, objective = "EMBER_LINK_PROCESSING_DEFEND_OBJECTIVE", task_groups = 14, squads = {"PROCESSING_DEFEND_DEFENSE_C_SQUAD", "PROCESSING_DEFEND_DEFENSE_D_SQUAD", "PROCESSING_DEFEND_MELEE_B_SQUAD", "PROCESSING_DEFEND_MELEE_C_SQUAD", "PROCESSING_DEFEND_SUPPORT_B_SQUAD"}},
    {name = "processing_wave3", region = 56, objective = "EMBER_LINK_PROCESSING_DEFEND_OBJECTIVE", task_groups = 14, squads = {"PROCESSING_DEFEND_ANCHOR_SQUAD", "PROCESSING_DEFEND_MELEE_D_SQUAD"}},
    {name = "ready1_entry", region = 40, objective = "EMBER_CINDER_READY_ROOM_01_OBJECTIVE", task_groups = 6, squads = {"READY_ROOM_01_SUPPORT_A_SQUAD", "READY_ROOM_01_SUPPORT_B_SQUAD"}},
    {name = "ready1_reinforce", region = 40, objective = "EMBER_CINDER_READY_ROOM_01_OBJECTIVE", task_groups = 6, squads = {"READY_ROOM_01_MELEE_A_SQUAD", "READY_ROOM_01_SUPPORT_C_SQUAD"}},
    {name = "tumbler", region = 40, objective = "EMBER_CINDER_TUMBLER_OBJECTIVE", task_groups = 5, squads = {"TUMBLER_SUPPORT_A_SQUAD", "TUMBLER_SUPPORT_B_SQUAD"}},
    {name = "sunburn_east", region = 40, objective = "EMBER_CINDER_SUNBURN_OBJECTIVE", task_groups = 13, squads = {"SUNBURN_DECK_EAST_SUPPORT_A_SQUAD", "SUNBURN_DECK_EAST_SUPPORT_B_SQUAD"}},
    {name = "sunburn_bridge", region = 40, objective = "EMBER_CINDER_SUNBURN_OBJECTIVE", task_groups = 13, squads = {"SUNBURN_BRIDGE_SUPPORT_A_SQUAD", "SUNBURN_BRIDGE_SUPPORT_B_SQUAD"}},
    {name = "sunburn_west", region = 40, objective = "EMBER_CINDER_SUNBURN_OBJECTIVE", task_groups = 13, squads = {"SUNBURN_DECK_WEST_SUPPORT_A_SQUAD", "SUNBURN_DECK_WEST_SUPPORT_B_SQUAD", "SUNBURN_DECK_WEST_SUPPORT_C_SQUAD", "SUNBURN_DECK_WEST_SUPPORT_D_SQUAD"}},
    {name = "sunburn_secret", region = 40, objective = "EMBER_CINDER_SUNBURN_OBJECTIVE", task_groups = 13, squads = {"SUNBURN_SECRET_SUPPORT_A_SQUAD"}},
    {name = "ready2_entry", region = 40, objective = "EMBER_CINDER_READY_ROOM_02_OBJECTIVE", task_groups = 6, squads = {"READY_ROOM_02_MELEE_A_SQUAD", "READY_ROOM_02_RANGED_A_SQUAD", "READY_ROOM_02_RANGED_B_SQUAD"}},
    {name = "ready2_reinforce", region = 40, objective = "EMBER_CINDER_READY_ROOM_02_OBJECTIVE", task_groups = 6, squads = {"READY_ROOM_02_ANCHOR_SQUAD", "READY_ROOM_02_MELEE_B_SQUAD", "READY_ROOM_02_MELEE_C_SQUAD"}},
    {name = "chamber", region = 40, objective = "EMBER_CINDER_CHAMBER_OBJECTIVE", task_groups = 2, squads = {"CHAMBER_DEFENSE_A_SQUAD", "CHAMBER_SUPPORT_A_SQUAD"}},
    {name = "meat1", region = 40, objective = "EMBER_CINDER_MEAT_GRINDER_OBJECTIVE", task_groups = 5, squads = {"MEAT_GRINDER_DEFENSE_A_SQUAD", "MEAT_GRINDER_MELEE_A_SQUAD", "MEAT_GRINDER_SUPPORT_A_SQUAD"}},
    {name = "meat2", region = 40, objective = "EMBER_CINDER_MEAT_GRINDER_OBJECTIVE", task_groups = 5, squads = {"MEAT_GRINDER_DEFENSE_B_SQUAD", "MEAT_GRINDER_MELEE_B_SQUAD", "MEAT_GRINDER_SUPPORT_B_SQUAD"}},
    {name = "ascent", region = 40, objective = "EMBER_CINDER_ASCENT_OBJECTIVE", task_groups = 2, squads = {"ASCENT_SUPPORT_A_SQUAD", "ASCENT_SUPPORT_B_SQUAD"}},
    {name = "sunburn_retreat_a", region = 40, objective = "SUNBURN_SQUAD_RETREAT_INSTANCE_A_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"SUNBURN_SQUAD_RETREAT_INSTANCE_A_PREFAB_RETREAT_01_SQUAD"}},
    {name = "sunburn_retreat_b", region = 40, objective = "SUNBURN_SQUAD_RETREAT_INSTANCE_B_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"SUNBURN_SQUAD_RETREAT_INSTANCE_B_PREFAB_RETREAT_01_SQUAD"}},
    {name = "sunburn_retreat_c", region = 40, objective = "SUNBURN_SQUAD_RETREAT_INSTANCE_C_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"SUNBURN_SQUAD_RETREAT_INSTANCE_C_PREFAB_RETREAT_01_SQUAD"}},
    {name = "ascent_retreat_a", region = 40, objective = "ASCENT_SQUAD_RETREAT_INSTANCE_A_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"ASCENT_SQUAD_RETREAT_INSTANCE_A_PREFAB_RETREAT_01_SQUAD"}},
    {name = "ascent_retreat_b", region = 40, objective = "ASCENT_SQUAD_RETREAT_INSTANCE_B_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"ASCENT_SQUAD_RETREAT_INSTANCE_B_PREFAB_RETREAT_01_SQUAD"}},
    {name = "ascent_retreat_c", region = 40, objective = "ASCENT_SQUAD_RETREAT_INSTANCE_C_PREFAB_RETREAT_OBJECTIVE", task_groups = 1, squads = {"ASCENT_SQUAD_RETREAT_INSTANCE_C_PREFAB_RETREAT_01_SQUAD"}},
    {name = "foundry_entry", region = 40, objective = "EMBER_CINDER_FOUNDRY_OBJECTIVE", task_groups = 16, squads = {"FOUNDRY_RANGED_A_SQUAD", "FOUNDRY_SUPPORT_A_SQUAD", "FOUNDRY_SUPPORT_B_SQUAD", "FOUNDRY_SUPPORT_C_SQUAD"}},
    {name = "foundry_mid", region = 40, objective = "EMBER_CINDER_FOUNDRY_OBJECTIVE", task_groups = 16, squads = {"FOUNDRY_RANGED_B_SQUAD", "FOUNDRY_SUPPORT_D_SQUAD", "FOUNDRY_SUPPORT_E_SQUAD", "FOUNDRY_SUPPORT_F_SQUAD"}},
    {name = "foundry_final", region = 40, objective = "EMBER_CINDER_FOUNDRY_OBJECTIVE", task_groups = 16, squads = {"FOUNDRY_ANCHOR_A_SQUAD", "FOUNDRY_SUPPORT_G_SQUAD", "FOUNDRY_SUPPORT_H_SQUAD", "FOUNDRY_SUPPORT_I_SQUAD"}},
    {name = "access1", region = 0, objective = "EMBER_APEX_ACCESS_OBJECTIVE", task_groups = 14, squads = {"ACCESS_JUMP_ONE_SUPPORT_A_SQUAD", "ACCESS_JUMP_ONE_SUPPORT_B_SQUAD", "ACCESS_JUMP_ONE_SUPPORT_C_SQUAD"}},
    {name = "access2", region = 0, objective = "EMBER_APEX_ACCESS_OBJECTIVE", task_groups = 14, squads = {"ACCESS_JUMP_TWO_SUPPORT_A_SQUAD", "ACCESS_JUMP_TWO_SUPPORT_B_SQUAD", "ACCESS_JUMP_TWO_SUPPORT_C_SQUAD"}},
    -- The dispenser squads hold the security room, not the access approach. Their authored
    -- anchors (-363,2643,181.5) and (-360,2645,181.5) are both inside
    -- `security_center_player_trigger` (-387..-344.5, 2639.5..2662, 177..197), while every
    -- access volume lies east of x=-331 (jump one -331..-312, jump two -256.5..-224, inner
    -- door -224..-174). Under the access objective the native task selector pulled the two
    -- Electron Controllers out to those eastern areas -- through the security door and out of
    -- reach. No access task group could hold them, so pinning a pair of them never could work.
    -- The security objective owns this room, so they keep their own cost-selected tasks here.
    {name = "electron_controllers", region = 0, objective = "EMBER_APEX_SECURITY_OBJECTIVE", task_groups = 7, squads = {"DISPENSER_SUPPORT_A_SQUAD", "DISPENSER_SUPPORT_B_SQUAD"}},
    {name = "dispenser", region = 0, objective = "EMBER_APEX_SECURITY_OBJECTIVE", task_groups = 7, squads = {"DISPENSER_SUPPORT_C_SQUAD"}},
    {name = "security", region = 0, objective = "EMBER_APEX_SECURITY_OBJECTIVE", task_groups = 7, squads = {"SECURITY_LEDGE_SUPPORT_A_SQUAD", "SECURITY_LEDGE_SUPPORT_B_SQUAD"}},
    {name = "reactor_east_entry", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_EAST_OBJECTIVE", task_groups = 19, squads = {"REACTOR_CLAMSHELL_EAST_DEFENSE_A_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_A_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_B_SQUAD"}},
    {name = "reactor_east_reinforce", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_EAST_OBJECTIVE", task_groups = 19, squads = {"REACTOR_CLAMSHELL_EAST_DEFENSE_B_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_A_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_B_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_C_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_D_SQUAD"}},
    {name = "reactor_east_final", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_EAST_OBJECTIVE", task_groups = 19, squads = {"REACTOR_CLAMSHELL_EAST_ANCHOR_A_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_C_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_D_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_E_SQUAD", "REACTOR_CLAMSHELL_EAST_MELEE_F_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_E_SQUAD", "REACTOR_CLAMSHELL_EAST_SUPPORT_F_SQUAD"}},
    {name = "reactor_west_entry", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_WEST_OBJECTIVE", task_groups = 16, squads = {"REACTOR_CLAMSHELL_WEST_DEFENSE_A_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_A_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_B_SQUAD"}},
    {name = "reactor_west_reinforce", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_WEST_OBJECTIVE", task_groups = 16, squads = {"REACTOR_CLAMSHELL_WEST_DEFENSE_B_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_A_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_B_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_C_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_D_SQUAD"}},
    {name = "reactor_west_final", region = 0, objective = "EMBER_APEX_REACTOR_CLAMSHELL_WEST_OBJECTIVE", task_groups = 16, squads = {"REACTOR_CLAMSHELL_WEST_ANCHOR_A_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_C_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_D_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_E_SQUAD", "REACTOR_CLAMSHELL_WEST_MELEE_F_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_E_SQUAD", "REACTOR_CLAMSHELL_WEST_SUPPORT_F_SQUAD"}},
    {name = "coffin_east", region = 0, objective = "EMBER_APEX_REACTOR_COFFIN_OBJECTIVE", task_groups = 19, squads = {"REACTOR_COFFIN_EAST_ANCHOR_A_SQUAD", "REACTOR_COFFIN_EAST_DEFENSE_A_SQUAD", "REACTOR_COFFIN_EAST_DEFENSE_B_SQUAD", "REACTOR_COFFIN_EAST_SUPPORT_A_SQUAD", "REACTOR_COFFIN_EAST_SUPPORT_B_SQUAD", "REACTOR_COFFIN_EAST_SUPPORT_C_SQUAD", "REACTOR_COFFIN_EAST_SUPPORT_D_SQUAD"}},
    {name = "coffin_west", region = 0, objective = "EMBER_APEX_REACTOR_COFFIN_OBJECTIVE", task_groups = 19, squads = {"REACTOR_COFFIN_WEST_ANCHOR_A_SQUAD", "REACTOR_COFFIN_WEST_DEFENSE_A_SQUAD", "REACTOR_COFFIN_WEST_DEFENSE_B_SQUAD", "REACTOR_COFFIN_WEST_SUPPORT_A_SQUAD", "REACTOR_COFFIN_WEST_SUPPORT_B_SQUAD", "REACTOR_COFFIN_WEST_SUPPORT_C_SQUAD", "REACTOR_COFFIN_WEST_SUPPORT_D_SQUAD"}},
    {name = "coffin_interior", region = 0, objective = "EMBER_APEX_REACTOR_COFFIN_OBJECTIVE", task_groups = 19, squads = {"REACTOR_COFFIN_INTERIOR_SUPPORT_SQUAD"}},
}
