cmake_minimum_required(VERSION 4.1)
project(DnsRelay C)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)
add_executable(DnsRelay src/main.c
        include/infra/stl.h
        include/infra/logger.h
        include/dns/protocol.h
        include/dns/cache.h
        include/infra/config.h
        include/infra/socket.h
        include/server/server.h
        src/server/server.c
        src/infra/stl.c
        src/infra/logger.c
        src/infra/windows/sock_win.c
        src/infra/linux/sock_posix.c
        include/infra/sys.h
        src/infra/linux/sys_linux.c
        src/infra/windows/sys_win.c
        include/server/session.h
        src/server/session_factory.c
        src/dns/dns_resolver.c
        src/dns/dns_cache.c
        src/infra/config_center.c
        include/dns/id.h
        src/dns/id_factory.c
        include/server/daemon.h
        src/server/daemons.c)
target_link_libraries(DnsRelay PRIVATE Threads::Threads)
target_include_directories(DnsRelay PRIVATE ${CMAKE_SOURCE_DIR}/include)
