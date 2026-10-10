cmake_minimum_required(VERSION 3.22)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(opinion_cpp_client VERSION 0.1.0 LANGUAGES CXX C)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

option(OPINION_CLIENT_BUILD_EXAMPLES "Build Opinion examples" ON)
option(OPINION_CLIENT_BUILD_TESTS "Build Opinion unit tests" ON)

include(FetchContent)

FetchContent_Declare(
    json
    URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
    URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
)

FetchContent_Declare(
    ixwebsocket
    URL https://codeload.github.com/machinezone/IXWebSocket/tar.gz/64fae7676bd8fe31f7cb4bcde7a6841892dad65e
    URL_HASH SHA256=af641277dd79c8f0970a31b84beec223d8c683eb8a671b0cce20bd8a8f750878
)

FetchContent_Declare(
    secp256k1
    URL https://codeload.github.com/bitcoin-core/secp256k1/tar.gz/1a53f4961f337b4d166c25fce72ef0dc88806618
    URL_HASH SHA256=b51329bdd3b6c955380f4c4ca236d07dd8e109d4e869a4758be4602d8ed5fe31
)

FetchContent_Declare(
    ethash
    URL https://codeload.github.com/chfast/ethash/tar.gz/83bd5ad51acf7d7fdba215347cc805d51a194d2b
    URL_HASH SHA256=9c2b342411d6c68983a6b1cae752cf4b65d105b582b9e3f277f5112c9b76c90f
)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(ETHASH_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
FetchContent_MakeAvailable(json ixwebsocket secp256k1 ethash)

add_library(opinion_client
    src/sdk_error.cpp
    src/decimal_math.cpp
    src/uint256.cpp
    src/network.cpp
    src/environment.cpp
    src/http_client.cpp
    src/order_signer.cpp
    src/clob_client.cpp
    src/websocket_client.cpp
    src/orderbook.cpp
    src/user_stream.cpp
)
add_library(opinion::client ALIAS opinion_client)

target_compile_features(opinion_client PUBLIC cxx_std_20)
target_include_directories(opinion_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(opinion_client
    PUBLIC
        CURL::libcurl
        OpenSSL::Crypto
        OpenSSL::SSL
        Threads::Threads
        ixwebsocket
        secp256k1
        ethash
        nlohmann_json::nlohmann_json
)
target_compile_options(opinion_client PRIVATE -Wall -Wextra -Wpedantic)

if(OPINION_CLIENT_BUILD_EXAMPLES)
    add_executable(opinion_markets examples/markets_example.cpp)
    target_link_libraries(opinion_markets PRIVATE opinion::client)
    add_executable(opinion_sign examples/sign_example.cpp)
    target_link_libraries(opinion_sign PRIVATE opinion::client)
    add_executable(opinion_ws examples/ws_example.cpp)
    target_link_libraries(opinion_ws PRIVATE opinion::client)
endif()

if(OPINION_CLIENT_BUILD_TESTS)
    enable_testing()
    add_executable(opinion_tests tests/test_sdk.cpp)
    target_link_libraries(opinion_tests PRIVATE opinion::client)
    add_test(NAME opinion_tests COMMAND opinion_tests)
    add_executable(opinion_hot_path_tests tests/test_hot_paths.cpp)
    target_link_libraries(opinion_hot_path_tests PRIVATE opinion::client)
    add_test(NAME opinion_hot_path_tests COMMAND opinion_hot_path_tests)
    add_executable(opinion_rest_tests tests/test_rest.cpp)
    target_link_libraries(opinion_rest_tests PRIVATE opinion::client)
    add_test(NAME opinion_rest_tests COMMAND opinion_rest_tests)
endif()

install(TARGETS opinion_client ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/opinion DESTINATION include)

# Build explicitly; timings are diagnostic, not pass/fail tests.
add_executable(opinion_hot_paths EXCLUDE_FROM_ALL benchmarks/hot_paths.cpp)
target_link_libraries(opinion_hot_paths PRIVATE opinion::client)
