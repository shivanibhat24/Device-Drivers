cmake_minimum_required(VERSION 3.22)
project(ecu_gui VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find Qt6 ──────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Network SerialPort
)
# Qt Charts is optional — enable if you add a live chart widget later
find_package(Qt6 QUIET COMPONENTS Charts)

# ── Sources ────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/ConnectionManager.cpp
    src/CANParser.cpp
    src/ECUDashboard.cpp
    src/FaultInjector.cpp
    src/CANMonitor.cpp
    src/DTCViewer.cpp
)

set(HEADERS
    include/MainWindow.hpp
    include/ConnectionManager.hpp
    include/CANParser.hpp
    include/ECUDashboard.hpp
    include/FaultInjector.hpp
    include/CANMonitor.hpp
    include/DTCViewer.hpp
)

# Shared protocol header (same file used by firmware)
set(SHARED_HEADERS
    ../../firmware/include/ecu_protocol.hpp
)

qt_add_executable(ecu_gui
    WIN32
    ${SOURCES}
    ${HEADERS}
    ${SHARED_HEADERS}
    resources/resources.qrc
)

target_include_directories(ecu_gui PRIVATE
    include
    ../../firmware/include
)

set(QT_LINK_LIBS
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialPort
)

# Link Charts only if found
if(Qt6Charts_FOUND)
    list(APPEND QT_LINK_LIBS Qt6::Charts)
    target_compile_definitions(ecu_gui PRIVATE ECU_HAS_QTCHARTS)
endif()

target_link_libraries(ecu_gui PRIVATE ${QT_LINK_LIBS})

# ── Windows: copy Qt DLLs next to exe (windeployqt) ──
if(WIN32)
    add_custom_command(TARGET ecu_gui POST_BUILD
        COMMAND "${QT_DEPLOY_PREFIX}/../bin/windeployqt.exe"
                --no-translations
                $<TARGET_FILE:ecu_gui>
        COMMENT "Running windeployqt"
        VERBATIM
    )
endif()
