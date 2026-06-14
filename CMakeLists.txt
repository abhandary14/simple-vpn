cmake_minimum_required(VERSION 3.10)
project(tunnelink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(tunnelink src/main.cpp src/crypto.cpp src/tun.cpp)
target_include_directories(tunnelink PRIVATE src)
target_link_libraries(tunnelink PRIVATE OpenSSL::Crypto)

enable_testing()
add_executable(test_crypto tests/test_crypto.cpp src/crypto.cpp)
target_include_directories(test_crypto PRIVATE src)
target_link_libraries(test_crypto PRIVATE OpenSSL::Crypto)
add_test(NAME crypto_roundtrip COMMAND test_crypto)