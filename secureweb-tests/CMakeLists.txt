# SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(SecureWebTransportTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/owncloudlib.h" "#pragma once\n#define OWNCLOUDSYNC_EXPORT\n")
add_executable(SecureWebReplyTest ../test/testsecurewebreply.cpp ../src/libsync/securewebreply.cpp ../src/libsync/securewebreply.h)
target_include_directories(SecureWebReplyTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(SecureWebReplyTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME SecureWebReplyTest COMMAND SecureWebReplyTest)
