cmake_minimum_required(VERSION 3.24)

project(TradeTogether VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE REQUIRED)

set(PROJECT_SOURCES
    src/PCH.h
    src/main.cpp
    src/Config.h
    src/Config.cpp
    src/Input.h
    src/Input.cpp
    src/Offer.h
    src/Offer.cpp
    src/Protocol.h
    src/Protocol.cpp
    src/Trade.h
    src/Trade.cpp
    src/UdpTransport.h
    src/UdpTransport.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES ${PROJECT_SOURCES}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive-)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 bcrypt)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${PROJECT_SOURCES})

set(PACKAGE_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/Data/SKSE/Plugins")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${PACKAGE_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMENT "Copying TradeTogether.dll to package/Data/SKSE/Plugins"
)
