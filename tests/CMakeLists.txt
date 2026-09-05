cmake_minimum_required(VERSION 3.20)
project(SunrisePortableTests LANGUAGES CXX)
enable_testing()
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
