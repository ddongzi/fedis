cmake_minimum_required(VERSION 4.0)
project(Fedis VERSION 1.0)

add_definitions(-DPROJECT_ROOT="${PROJECT_SOURCE_DIR}")


# 库文件
add_subdirectory(lib/googletest-1.17.0)
message(STATUS "GTest: ${gtest_SOURCE_DIR}")


find_package(OpenSSL REQUIRED)

# 执行文件
add_executable(fedis
        src/ae.c src/aof.c src/client.c src/conf.c src/crypto.c src/db.c
        src/dict.c src/list.c src/log.c src/net.c src/notify.c
        src/rdb.c src/redis.c src/repli.c src/resp.c src/rio.c src/robj.c
        src/sds.c src/util.c
        src/main.c
)
target_include_directories(fedis PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(fedis PRIVATE OpenSSL::SSL)
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")


# 测试
enable_testing()
add_executable(unit_tests
        test/test_resp.cpp
        test/test_robj.cpp
        test/test_command.cpp
        src/resp.c src/robj.c src/sds.c
        src/log.c
)
target_include_directories(unit_tests PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(unit_tests gtest gtest_main)

# client
add_executable( client
        client/client.c
        src/resp.c
        src/log.c
        src/linenoise.c
        src/sds.c
)
target_include_directories(client PUBLIC ${PROJECT_SOURCE_DIR}/include)
