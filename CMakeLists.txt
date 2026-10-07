cmake_minimum_required(VERSION 3.22)

# Older fetched dependencies still declare very old cmake_minimum_required values.
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

set(OPINION_CLIENT_TOP_LEVEL_DEFAULT OFF)
if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
    set(OPINION_CLIENT_TOP_LEVEL_DEFAULT ON)
endif()

option(OPINION_BUILD_EXAMPLES "Build example executables" ${OPINION_CLIENT_TOP_LEVEL_DEFAULT})
option(OPINION_BUILD_TESTS "Build unit tests" ${OPINION_CLIENT_TOP_LEVEL_DEFAULT})
unset(OPINION_CLIENT_TOP_LEVEL_DEFAULT)

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

function(_opinion_load_dependencies)
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)

    set(USE_TLS ON)
    set(USE_OPEN_SSL ON)
    set(IXWEBSOCKET_INSTALL OFF)
    set(SECP256K1_ENABLE_MODULE_RECOVERY ON)
    set(SECP256K1_BUILD_TESTS OFF)
    set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF)
    set(SECP256K1_BUILD_BENCHMARK OFF)
    set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)

    FetchContent_MakeAvailable(json ixwebsocket secp256k1)
endfunction()

_opinion_load_dependencies()

set_target_properties(ixwebsocket secp256k1 PROPERTIES PUBLIC_HEADER "")

add_library(opinion_client
    src/decimal_math.cpp
    src/environment.cpp
    src/http_client.cpp
    src/keccak.cpp
    src/sdk_error.cpp
    src/order_signer.cpp
    src/orderbook.cpp
    src/clob_client.cpp
    src/websocket_client.cpp
    src/user_stream.cpp
)
add_library(opinion::client ALIAS opinion_client)

set_target_properties(opinion_client PROPERTIES
    OUTPUT_NAME opinion_client
    EXPORT_NAME client
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
target_compile_features(opinion_client PUBLIC cxx_std_20)
target_compile_options(opinion_client PRIVATE -Wall -Wextra -Wpedantic)

target_include_directories(opinion_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(opinion_client
    PUBLIC
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
        ixwebsocket
        secp256k1
)

if(OPINION_BUILD_EXAMPLES)
    add_executable(public_markets examples/public_markets.cpp)
    target_link_libraries(public_markets PRIVATE opinion_client)
    target_compile_options(public_markets PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ws_subscribe examples/ws_subscribe.cpp)
    target_link_libraries(ws_subscribe PRIVATE opinion_client)
    target_compile_options(ws_subscribe PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPINION_BUILD_TESTS)
    add_executable(opinion_tests tests/test_sdk.cpp)
    target_link_libraries(opinion_tests PRIVATE opinion_client)
    target_compile_options(opinion_tests PRIVATE -Wall -Wextra -Wpedantic)
    enable_testing()
    add_test(NAME opinion_tests COMMAND opinion_tests)
endif()

install(TARGETS opinion_client
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/opinion DESTINATION include)
