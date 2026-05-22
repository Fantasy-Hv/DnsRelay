cmake_minimum_required(VERSION 4.1)
project(DnsRelay C)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)

# ========== 共享源文件（静态库） ==========
set(LIB_SOURCES
        src/infra/stl.c
        src/infra/logger.c
        src/infra/sock_posix.c
        src/infra/utils.c
        src/infra/config_center.c
        src/infra/exception.c
        src/dns/dns_resolver.c
        src/dns/dns_cache.c
        src/dns/id_factory.c
        src/server/server.c
        src/server/session_factory.c
        src/server/daemons.c
)

set(LIB_HEADERS
        include/infra/stl.h
        include/infra/logger.h
        include/infra/config.h
        include/infra/socket.h
        include/infra/utils.h
        include/infra/exception.h
        include/dns/protocol.h
        include/dns/cache.h
        include/dns/id.h
        include/server/server.h
        include/server/session.h
        include/server/daemon.h
)

add_library(DnsRelayLib STATIC ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(DnsRelayLib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(DnsRelayLib PUBLIC Threads::Threads)

# ========== 主程序 ==========
add_executable(DnsRelay src/main.c)
target_link_libraries(DnsRelay PRIVATE DnsRelayLib)

# ========== 单测 ==========
include(CTest)

# 简单的断言宏
add_library(test_utils_header INTERFACE)
target_include_directories(test_utils_header INTERFACE ${CMAKE_SOURCE_DIR}/test)

function(add_module_test test_name test_src)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE DnsRelayLib test_utils_header)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# --- 各模块单测 ---
add_module_test(test_stl test/test_stl.c)
add_module_test(test_config test/test_config.c)
add_module_test(test_exception test/test_exception.c)
add_module_test(test_socket test/test_socket.c)
add_module_test(test_utils test/test_platform_utils.c)
add_module_test(test_id test/test_id.c)
add_module_test(test_protocol test/test_protocol.c)
add_module_test(test_cache test/test_cache.c)
add_module_test(test_session test/test_session.c)
add_module_test(test_daemon test/test_daemon.c)
add_module_test(test_logger test/test_logger.c)
