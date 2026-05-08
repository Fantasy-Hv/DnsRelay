cmake_minimum_required(VERSION 4.1)
project(DnsRelay C)

set(CMAKE_C_STANDARD 23)

add_executable(DnsRelay src/service/ui/main.c
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
        src/infra/thread.c
        src/infra/socket.c)
target_include_directories(DnsRelay PRIVATE ${CMAKE_SOURCE_DIR}/include)