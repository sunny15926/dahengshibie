cmake_minimum_required(VERSION 3.16)

project(dahengshibie LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# ---- 第三方依赖 ----
find_package(OpenCV REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

# ---- 大恒 Galaxy SDK ----
# 头文件（GxIAPI.h / DxImageProc.h 等）已内置在 include/sdk/，源码以 #include <sdk/...> 引入，
# 因此项目不依赖任何外部 SDK 路径。
# 动态库 libgxiapi.so 由大恒官方 Galaxy 驱动安装到 /usr/lib，直接 -lgxiapi 链接即可。

add_executable(dahengshibie
  src/main.cpp
  src/traditional_detector.cpp
  src/daheng_camera.cpp
  src/rm_log.cpp
)

target_include_directories(dahengshibie PRIVATE include)

target_link_libraries(dahengshibie
  ${OpenCV_LIBS}
  yaml-cpp
  spdlog::spdlog
  gxiapi
  Threads::Threads
)
