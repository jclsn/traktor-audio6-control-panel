cmake_minimum_required(VERSION 3.20)
project(traktor-audio6-panel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy and editors
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

add_executable(traktor-audio6-panel
  src/main.cpp
  src/backend.cpp src/backend.h
  src/widgets.cpp src/widgets.h
  src/panel.cpp src/panel.h
)
target_compile_definitions(traktor-audio6-panel PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_compile_options(traktor-audio6-panel PRIVATE -Wall -Wextra)
target_link_libraries(traktor-audio6-panel PRIVATE Qt6::Widgets ALSA::ALSA Threads::Threads)

install(TARGETS traktor-audio6-panel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/traktor-audio6-panel.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/70-traktor-audio6.rules
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
