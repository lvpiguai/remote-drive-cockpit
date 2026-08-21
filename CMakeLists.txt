cmake_minimum_required(VERSION 3.16)

project(remote_drive_cockpit LANGUAGES CXX)

include(CTest)

find_package(Boost 1.75 REQUIRED COMPONENTS system)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    proto/remote_drive.proto
    proto/remote_drive_cockpit_config.proto
)

add_library(cockpit_protocol
    ${PROTO_SRCS}
    src/protocol_codec.cpp
)

target_include_directories(cockpit_protocol PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(cockpit_protocol PUBLIC ${Protobuf_LIBRARIES})

add_library(cockpit_common
    src/websocket_server.cpp
)

target_include_directories(cockpit_common PUBLIC
    include
)
target_link_libraries(cockpit_common PUBLIC Boost::system)

add_library(cockpit_devices
    src/input_device_reader.cpp
)

target_include_directories(cockpit_devices PUBLIC
    include
)

add_library(cockpit_core
    src/cockpit.cpp
    src/web_protocol.cpp
    src/control_command_generator.cpp
    src/udp_channel.cpp
    src/vehicle_state_cache.cpp
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
      tests/input_pipeline_test.cpp
  )
  target_link_libraries(input_pipeline_test PRIVATE cockpit_core)
  add_test(NAME input_pipeline_test COMMAND input_pipeline_test)

  add_executable(web_protocol_test
      tests/web_protocol_test.cpp
  )
  target_link_libraries(web_protocol_test PRIVATE cockpit_core)
  add_test(NAME web_protocol_test COMMAND web_protocol_test)

  add_executable(websocket_server_test
      tests/websocket_server_test.cpp
  )
  target_link_libraries(websocket_server_test PRIVATE cockpit_common Threads::Threads)
  add_test(NAME websocket_server_test COMMAND websocket_server_test)

  add_executable(protocol_codec_test
      tests/protocol_codec_test.cpp
  )
  target_link_libraries(protocol_codec_test PRIVATE cockpit_protocol)
  add_test(NAME protocol_codec_test COMMAND protocol_codec_test)

  add_executable(vehicle_state_cache_test
      tests/vehicle_state_cache_test.cpp
  )
  target_link_libraries(vehicle_state_cache_test PRIVATE cockpit_core)
  add_test(NAME vehicle_state_cache_test COMMAND vehicle_state_cache_test)

  add_executable(udp_channel_test
      tests/udp_channel_test.cpp
  )
  target_link_libraries(udp_channel_test PRIVATE cockpit_core)
  add_test(NAME udp_channel_test COMMAND udp_channel_test)

  add_executable(input_device_reader_test
      tests/input_device_reader_test.cpp
  )
  target_link_libraries(input_device_reader_test PRIVATE cockpit_devices)
  add_test(NAME input_device_reader_test COMMAND input_device_reader_test)

endif()
