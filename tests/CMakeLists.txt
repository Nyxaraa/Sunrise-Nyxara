cmake_minimum_required(VERSION 3.20)
project(SunrisePortableTests LANGUAGES CXX)
enable_testing()
add_executable(ending_retirement_test ending_retirement_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sensor_auth_encoder.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sensor_auth_blocks.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sensor_auth_bodies.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp
    ../Sunrise/src/middleware/encoding/bit_raw.cpp)
target_compile_features(ending_retirement_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ending_retirement_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(ending_retirement_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME ending_retirement COMMAND ending_retirement_test)
add_executable(membership_replication_test membership_replication_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_replicate_membership_encoder.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_membership_member_writer.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_membership_region_writer.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp
    ../Sunrise/src/middleware/encoding/bit_raw.cpp)
target_compile_features(membership_replication_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(membership_replication_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(membership_replication_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME membership_replication COMMAND membership_replication_test)

add_executable(authority_cleanup_test authority_cleanup_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_entity_authority_parser.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_host_control_codec.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(authority_cleanup_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(authority_cleanup_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(authority_cleanup_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME authority_cleanup COMMAND authority_cleanup_test)
add_executable(peer_external_ack_test peer_external_ack_test.cpp
    ../Sunrise/src/middleware/gameplay/peer/established_packet.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp
    ../Sunrise/src/middleware/encoding/bit_raw.cpp)
target_compile_features(peer_external_ack_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(peer_external_ack_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(peer_external_ack_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME peer_external_ack COMMAND peer_external_ack_test)
add_executable(bap_frame_batch_test bap_frame_batch_test.cpp)
target_compile_features(bap_frame_batch_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(bap_frame_batch_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(bap_frame_batch_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME bap_frame_batch COMMAND bap_frame_batch_test)
add_executable(mission_event_batch_test mission_event_batch_test.cpp)
target_compile_features(mission_event_batch_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(mission_event_batch_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(mission_event_batch_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME mission_event_batch COMMAND mission_event_batch_test)
add_executable(socket_row_relocation_test socket_row_relocation_test.cpp)
target_compile_features(socket_row_relocation_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(socket_row_relocation_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(socket_row_relocation_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME socket_row_relocation
    COMMAND socket_row_relocation_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/socket_rows.hex")
add_executable(lore_visibility_test lore_visibility_test.cpp)
target_compile_features(lore_visibility_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(lore_visibility_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(lore_visibility_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME lore_visibility COMMAND lore_visibility_test)
add_executable(incident_payload_test
    incident_payload_test.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_incident_parser.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_incident_encoder.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_cinematic_incident_codec.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_player_trigger_incident_codec.cpp
    ../Sunrise/src/server/activity/mission/mission_script_cinematic.cpp)
target_compile_features(incident_payload_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(incident_payload_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(incident_payload_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME incident_payload COMMAND incident_payload_test)
add_executable(spawn_override_test spawn_override_test.cpp)
target_compile_features(spawn_override_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(spawn_override_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(spawn_override_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME spawn_override COMMAND spawn_override_test)
add_executable(squad_sense_test squad_sense_test.cpp)
target_compile_features(squad_sense_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(squad_sense_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(squad_sense_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME squad_sense COMMAND squad_sense_test)
add_executable(ghost_link_test ghost_link_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sense_update_decoder.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(ghost_link_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ghost_link_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(ghost_link_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME ghost_link COMMAND ghost_link_test)

add_executable(combatant_path_test combatant_path_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sense_update_decoder.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_squad_auth_body_codec.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(combatant_path_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(combatant_path_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(combatant_path_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME combatant_path COMMAND combatant_path_test)

add_executable(mission_auth_patch_test mission_auth_patch_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_squad_auth_body_codec.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(mission_auth_patch_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(mission_auth_patch_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(mission_auth_patch_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME mission_auth_patch COMMAND mission_auth_patch_test)

add_executable(darkness_zone_test darkness_zone_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sensor_auth_bodies.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(darkness_zone_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(darkness_zone_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(darkness_zone_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME darkness_zone COMMAND darkness_zone_test)

add_executable(player_life_test player_life_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sensor_auth_bodies.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(player_life_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(player_life_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(player_life_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME player_life COMMAND player_life_test)

add_executable(interactable_object_test interactable_object_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sense_update_decoder.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(interactable_object_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(interactable_object_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(interactable_object_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME interactable_object COMMAND interactable_object_test)

add_executable(directive_navpoint_test directive_navpoint_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_scriptable_auth_fixed_body_codec.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(directive_navpoint_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(directive_navpoint_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(directive_navpoint_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME directive_navpoint COMMAND directive_navpoint_test)

add_executable(mission_effect_test mission_effect_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_scriptable_auth_body_codec.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(mission_effect_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(mission_effect_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(mission_effect_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME mission_effect COMMAND mission_effect_test)

add_executable(damage_monitor_test damage_monitor_test.cpp
    ../Sunrise/src/middleware/bap/activity_message/activity_sense_update_decoder.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp)
target_compile_features(damage_monitor_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(damage_monitor_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(damage_monitor_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME damage_monitor COMMAND damage_monitor_test)

add_executable(music_section_test music_section_test.cpp
    ../Sunrise/src/middleware/encoding/bit_reader.cpp
    ../Sunrise/src/middleware/encoding/bit_writer.cpp)
target_compile_features(music_section_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(music_section_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(music_section_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME music_section COMMAND music_section_test)

add_executable(behavior_scope_test behavior_scope_test.cpp)
target_compile_features(behavior_scope_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(behavior_scope_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(behavior_scope_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME behavior_scope COMMAND behavior_scope_test)

add_executable(mission_seed_world_change_test mission_seed_world_change_test.cpp)
target_compile_features(mission_seed_world_change_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(mission_seed_world_change_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(mission_seed_world_change_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME mission_seed_world_change COMMAND mission_seed_world_change_test)
