cmake_minimum_required(VERSION 3.16)

project(remote_drive_cockpit LANGUAGES CXX)

include(CTest)

find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/remote_drive.proto)

add_library(cockpit_protocol
    ${PROTO_SRCS}
    src/protocol/udp_codec.cpp
)

target_include_directories(cockpit_protocol PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(cockpit_protocol PUBLIC ${Protobuf_LIBRARIES})

add_library(cockpit_common
    src/common/websocket_server.cpp
)

target_include_directories(cockpit_common PUBLIC
    include
)
target_link_libraries(cockpit_common PUBLIC OpenSSL::Crypto)

add_library(cockpit_devices
    src/devices/input_device_reader.cpp
)

target_include_directories(cockpit_devices PUBLIC
    include
)

add_library(cockpit_core
    src/cockpit/cockpit.cpp
    src/cockpit/cockpit_web_protocol.cpp
    src/cockpit/control_command_generator.cpp
    src/cockpit/control_command_sender.cpp
    src/cockpit/udp_channel.cpp
    src/cockpit/vehicle_heartbeat_cache.cpp
    src/cockpit/vehicle_state_cache.cpp
)

target_include_directories(cockpit_core PUBLIC
    include
)
target_link_libraries(cockpit_core PUBLIC
    cockpit_common
    cockpit_devices
    cockpit_protocol
)

add_executable(cockpit apps/main.cpp)
target_link_libraries(cockpit PRIVATE cockpit_core)

if(BUILD_TESTING)
  add_executable(input_pipeline_test
      tests/cockpit/input_pipeline_test.cpp
  )
  target_link_libraries(input_pipeline_test PRIVATE cockpit_core)
  add_test(NAME input_pipeline_test COMMAND input_pipeline_test)

  add_executable(control_command_sender_test
      tests/cockpit/control_command_sender_test.cpp
  )
  target_link_libraries(control_command_sender_test PRIVATE cockpit_core)
  add_test(NAME control_command_sender_test COMMAND control_command_sender_test)

  add_executable(cockpit_web_protocol_test
      tests/cockpit/cockpit_web_protocol_test.cpp
  )
  target_link_libraries(cockpit_web_protocol_test PRIVATE cockpit_core)
  add_test(NAME cockpit_web_protocol_test COMMAND cockpit_web_protocol_test)

  add_executable(vehicle_heartbeat_cache_test
      tests/cockpit/vehicle_heartbeat_cache_test.cpp
  )
  target_link_libraries(vehicle_heartbeat_cache_test PRIVATE cockpit_core)
  add_test(NAME vehicle_heartbeat_cache_test
           COMMAND vehicle_heartbeat_cache_test)

  add_executable(vehicle_state_cache_test
      tests/cockpit/vehicle_state_cache_test.cpp
  )
  target_link_libraries(vehicle_state_cache_test PRIVATE cockpit_core)
  add_test(NAME vehicle_state_cache_test COMMAND vehicle_state_cache_test)

  add_executable(udp_channel_test
      tests/cockpit/udp_channel_test.cpp
  )
  target_link_libraries(udp_channel_test PRIVATE cockpit_core)
  add_test(NAME udp_channel_test COMMAND udp_channel_test)

  add_executable(input_device_reader_test
      tests/devices/input_device_reader_test.cpp
  )
  target_link_libraries(input_device_reader_test PRIVATE cockpit_devices)
  add_test(NAME input_device_reader_test COMMAND input_device_reader_test)

endif()
