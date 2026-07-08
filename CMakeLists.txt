cmake_minimum_required(VERSION 3.16)
project(binance_lob_capture CXX)

# Use modern BoostConfig.cmake instead of the deprecated FindBoost module.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)  # Beast/Asio are header-only

# simdjson vendored as the amalgamated single-source release.
# Built as its own library so our strict warning flags don't apply to
# third-party code.
add_library(simdjson STATIC third_party/simdjson.cpp)
target_include_directories(simdjson PUBLIC third_party)

add_library(blc_core STATIC
  src/utils.cpp
  src/csv_writer.cpp
  src/order_book.cpp
  src/config.cpp
  src/binance_parser.cpp
  src/market_data_writer.cpp
  src/book_pipeline.cpp
  src/replay_engine.cpp
)
target_include_directories(blc_core PUBLIC src)
target_compile_options(blc_core PRIVATE -Wall -Wextra)
target_link_libraries(blc_core PUBLIC simdjson)

add_executable(binance_capture
  src/main.cpp
  src/websocket_client.cpp
)
target_compile_options(binance_capture PRIVATE -Wall -Wextra)
target_link_libraries(binance_capture PRIVATE
  blc_core
  Boost::headers
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

enable_testing()
add_executable(unit_tests tests/unit_tests.cpp)
target_compile_options(unit_tests PRIVATE -Wall -Wextra)
target_link_libraries(unit_tests PRIVATE blc_core)
add_test(NAME unit_tests COMMAND unit_tests)

add_executable(sim_live tests/sim_live.cpp)
target_compile_options(sim_live PRIVATE -Wall -Wextra)
target_link_libraries(sim_live PRIVATE blc_core)
