cmake_minimum_required(VERSION 3.16)
project(kavtor VERSION 0.25.0 LANGUAGES CXX)

configure_file(src/Version.h.in generated/Version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
include(CTest)
include(GNUInstallDirs)
qt_standard_project_setup()

# Runtime HTML producers must be available in out-of-tree builds as well as installs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/share)
file(GLOB runtime_html CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/share/*.html)
foreach(asset IN LISTS runtime_html)
  get_filename_component(asset_name ${asset} NAME)
  configure_file(${asset} ${CMAKE_CURRENT_BINARY_DIR}/share/${asset_name} COPYONLY)
endforeach()
qt_add_resources(app_resources "resources.qrc")

set(CORE_SOURCES
    src/core/MoveTransition.cpp
    src/core/MoveTransition.h
    src/core/AmcpClient.cpp
    src/core/AmcpClient.h
    src/core/SwitcherEngine.cpp
    src/core/ManualTransition.cpp
    src/core/SwitcherEngine.h
    src/core/PanelProtocol.cpp
    src/core/PanelProtocol.h
    src/core/SystemMonitor.cpp
    src/core/SystemMonitor.h
    src/core/SuperSource.cpp
    src/core/SuperSource.h
    src/core/NativeMultiview.cpp
    src/core/NativeMultiview.h
    src/core/OscListener.cpp
    src/core/OscListener.h
    src/core/Transition.h
    src/core/Transition.cpp
    src/core/SpeedEditorHid.cpp
    src/core/SpeedEditorHid.h
    src/config/Configuration.cpp
    src/config/Configuration.h
)

set(UI_SOURCES
    src/ui/SuperSourceEditor.cpp
    src/ui/SuperSourceEditor.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/SetupWorkspace.cpp
    src/ui/SetupWorkspace.h
)

add_executable(kavtor
    src/main.cpp
    src/Logging.h
    src/Logging.cpp
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${app_resources}
)

target_include_directories(kavtor PRIVATE src ${HIDAPI_INCLUDE_DIRS})
target_link_libraries(kavtor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    PkgConfig::HIDAPI
)

if(BUILD_TESTING)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test-management tests/test_management.cpp ${CORE_SOURCES} ${UI_SOURCES} src/Logging.cpp ${app_resources})
target_include_directories(test-management PRIVATE src)
target_link_libraries(test-management PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test PkgConfig::HIDAPI)
add_test(NAME ManagementTest COMMAND test-management)
set_tests_properties(ManagementTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test-amcpclient
    tests/test_amcpclient.cpp
    src/Logging.cpp
    src/core/AmcpClient.cpp
    src/core/AmcpClient.h
)

target_include_directories(test-amcpclient PRIVATE src)
target_link_libraries(test-amcpclient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_executable(test-switchengine
    src/core/PanelProtocol.cpp
    src/core/PanelProtocol.h
    tests/test_switchengine.cpp
    src/Logging.cpp
    src/core/AmcpClient.cpp
    src/core/AmcpClient.h
    src/core/SwitcherEngine.cpp
    src/core/MoveTransition.cpp
    src/core/ManualTransition.cpp
    src/core/SwitcherEngine.h
    src/core/SystemMonitor.cpp
    src/core/SystemMonitor.h
    src/core/SuperSource.cpp
    src/core/SuperSource.h
    src/core/NativeMultiview.cpp
    src/core/NativeMultiview.h
    src/core/OscListener.cpp
    src/core/OscListener.h
    src/core/Transition.cpp
    src/core/Transition.h
    src/config/Configuration.cpp
    src/config/Configuration.h
)

target_include_directories(test-switchengine PRIVATE src)
target_link_libraries(test-switchengine PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
)

add_test(NAME AmcpClientTest COMMAND test-amcpclient)
add_test(NAME SwitcherEngineTest COMMAND test-switchengine)
set_tests_properties(AmcpClientTest SwitcherEngineTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
add_executable(test-systemmonitor tests/test_systemmonitor.cpp src/core/SystemMonitor.cpp src/core/SystemMonitor.h)
target_include_directories(test-systemmonitor PRIVATE src)
target_link_libraries(test-systemmonitor PRIVATE Qt6::Core Qt6::Test)
add_test(NAME SystemMonitorTest COMMAND test-systemmonitor)

add_test(NAME CliHelp COMMAND kavtor --help)
add_test(NAME CliVersion COMMAND kavtor --version)

endif()

install(TARGETS kavtor DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY config/examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kavtor/examples)
install(FILES ${runtime_html} DESTINATION ${CMAKE_INSTALL_DATADIR}/kavtor)
install(FILES docs/panel-protocol.md DESTINATION share/kavtor)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_GENERATE_LATEX NO)
  doxygen_add_docs(docs src README.md COMMENT "Generate kavtor API documentation")
endif()

if(BUILD_TESTING)
  find_program(KAVTOR_NODE_EXECUTABLE node)
  if(KAVTOR_NODE_EXECUTABLE)
    add_test(NAME WipeGeometry COMMAND ${KAVTOR_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wipe_geometry.js ${CMAKE_CURRENT_SOURCE_DIR}/share/wipe-matte.html)
  endif()
endif()

option(KAVTOR_BUILD_NDI_DIAGNOSTICS "Build optional NDI receiver timing probe (requires NDI SDK)" OFF)
if(KAVTOR_BUILD_NDI_DIAGNOSTICS)
    find_path(KAVTOR_NDI_INCLUDE_DIR Processing.NDI.Lib.h REQUIRED)
    find_library(KAVTOR_NDI_LIBRARY ndi REQUIRED)
    add_executable(kavtor-ndi-timing tools/ndi_timing_probe.cpp)
    target_include_directories(kavtor-ndi-timing PRIVATE ${KAVTOR_NDI_INCLUDE_DIR})
    target_link_libraries(kavtor-ndi-timing PRIVATE ${KAVTOR_NDI_LIBRARY})
endif()
