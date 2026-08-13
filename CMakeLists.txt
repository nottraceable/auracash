cmake_minimum_required(VERSION 3.18)
project(AuraCash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build unit tests" ON)

# Compiler options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -Wno-unused-but-set-variable")
endif()

# Find dependencies
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# Use pkg-config for OpenSSL and libevent
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(SECP256K1 REQUIRED libsecp256k1)

# Include directories for this project (including subdirectories)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mempool
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rpc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wallet
    ${CMAKE_CURRENT_SOURCE_DIR}/src/miner
)

# Core source files
set(CORE_SOURCES
    src/Keccak256.cpp
    src/AuraHash.cpp
    src/CoreTypes.cpp
    src/EmissionModel.cpp
    src/UTXOEngine.cpp
    src/Consensus.cpp
)

# Crypto
set(CRYPTO_SOURCES
    src/crypto/Secp256k1.cpp
)

# Mempool
set(MEMPOOL_SOURCES
    src/mempool/Mempool.cpp
)

# Network
set(NET_SOURCES
    src/net/NetManager.cpp
)

# RPC
set(RPC_SOURCES
    src/rpc/Json.cpp
    src/rpc/RpcServer.cpp
    src/rpc/RpcClient.cpp
)

# Wallet
set(WALLET_SOURCES
    src/wallet/Wallet.cpp
)

# Miner
set(MINER_SOURCES
    src/miner/Miner.cpp
)

# Common link libraries for all targets
set(COMMON_LIBS
    Threads::Threads
    ssl crypto
    ${CURL_LIBRARIES}
    ${LIBEVENT_LIBRARIES}
    ${SECP256K1_LIBRARIES}
)

# ==================== TARGETS ====================

# 1. Full Node Daemon
add_executable(auracash-node
    src/main.cpp
    ${CORE_SOURCES}
    ${CRYPTO_SOURCES}
    ${MEMPOOL_SOURCES}
    ${NET_SOURCES}
    ${RPC_SOURCES}
)
target_link_libraries(auracash-node ${COMMON_LIBS})

# 2. CLI Wallet
add_executable(auracash-wallet
    src/wallet/wallet_main.cpp
    ${WALLET_SOURCES}
    ${CORE_SOURCES}
    ${CRYPTO_SOURCES}
    ${RPC_SOURCES}
)
target_link_libraries(auracash-wallet ${COMMON_LIBS})

# 3. Standalone Miner
add_executable(auracash-miner
    ${MINER_SOURCES}
    ${CORE_SOURCES}
    ${CRYPTO_SOURCES}
    ${RPC_SOURCES}
)
target_link_libraries(auracash-miner ${COMMON_LIBS})

# Unit tests
if(BUILD_TESTS)
    enable_testing()
    add_executable(auracash-tests
        tests/test_main.cpp
        ${CORE_SOURCES}
        ${CRYPTO_SOURCES}
        ${MEMPOOL_SOURCES}
        ${NET_SOURCES}
        ${RPC_SOURCES}
        ${WALLET_SOURCES}
    )
    target_link_libraries(auracash-tests ${COMMON_LIBS})
    add_test(NAME auracash-tests COMMAND auracash-tests)
endif()

# Install
install(TARGETS auracash-node auracash-wallet auracash-miner DESTINATION bin)
