cmake_minimum_required(VERSION 3.10)
project(tunnelink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(tunnelink src/main.cpp src/crypto.cpp src/tun.cpp src/udp.cpp src/loop.cpp)
target_include_directories(tunnelink PRIVATE src)
target_link_libraries(tunnelink PRIVATE OpenSSL::Crypto)

enable_testing()
add_executable(test_crypto tests/test_crypto.cpp src/crypto.cpp)
target_include_directories(test_crypto PRIVATE src)
target_link_libraries(test_crypto PRIVATE OpenSSL::Crypto)
add_test(NAME crypto_roundtrip COMMAND test_crypto)

add_executable(test_udp tests/test_udp.cpp src/udp.cpp)
target_include_directories(test_udp PRIVATE src)
add_test(NAME udp_loopback COMMAND test_udp)

add_executable(test_loop tests/test_loop.cpp src/loop.cpp src/crypto.cpp src/udp.cpp)
target_include_directories(test_loop PRIVATE src)
target_link_libraries(test_loop PRIVATE OpenSSL::Crypto)
add_test(NAME loop_integration COMMAND test_loop)

add_test(
    NAME server_setup_script
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_server.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/setup_server.sh
)
