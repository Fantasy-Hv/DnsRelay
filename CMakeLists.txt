cmake_minimum_required(VERSION 4.1)
project(DnsRelay C)

set(CMAKE_C_STANDARD 23)

add_executable(DnsRelay src/service/main.c
        include/infra/stl.h
        include/infra/logger.h
        include/dns/protocol.h
        include/dns/cache.h
        include/dns/config.h
        include/infra/thread.h
        include/infra/socket.h
        include/dns/server.h
        src/service/server.c
        src/infra/stl.c
        src/infra/logger.c
        src/infra/linux/thread_posix.c
        src/infra/windows/sock_win.c
        src/infra/linux/sock_linux.c
        src/infra/windows/thread_win.c
        include/infra/sys.h
        src/infra/linux/sys_linux.c
        src/infra/windows/sys_win.c)
target_include_directories(DnsRelay PRIVATE ${CMAKE_SOURCE_DIR}/include)