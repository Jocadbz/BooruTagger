cmake_minimum_required(VERSION 3.22)
project(BooruTagger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Sql OpenGL OpenGLWidgets Concurrent Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
find_package(Qt6Keychain QUIET)

add_library(booru_core
    src/core/Types.cpp
    src/core/Types.h
    src/tagging/TagChanges.cpp
    src/tagging/TagChanges.h
    src/network/HttpClient.cpp
    src/network/HttpClient.h
    src/providers/BooruProvider.cpp
    src/providers/BooruProvider.h
    src/providers/DanbooruProvider.cpp
    src/providers/DanbooruProvider.h
    src/providers/ShimmieProvider.cpp
    src/providers/ShimmieProvider.h
    src/database/Database.cpp
    src/database/Database.h
    src/sessions/Session.h
    src/sessions/Session.cpp
    src/sessions/SessionEngine.cpp
    src/sessions/SessionEngine.h
    src/settings/Credentials.cpp
    src/settings/Credentials.h
    src/tagging/Completion.cpp
    src/tagging/Completion.h
)
target_include_directories(booru_core PUBLIC src)
target_link_libraries(booru_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)
target_compile_definitions(booru_core PUBLIC BOORUTAGGER_VERSION="${PROJECT_VERSION}")
if(TARGET qt6keychain)
    target_link_libraries(booru_core PRIVATE qt6keychain)
    target_compile_definitions(booru_core PUBLIC BOORUTAGGER_KEYCHAIN=1)
endif()

add_library(booru_widgets
    src/ui/TagEditor.cpp
    src/ui/TagEditor.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/Dialogs.cpp
    src/ui/Dialogs.h
    src/media/ImageView.cpp
    src/media/ImageView.h
    src/media/MpvWidget.cpp
    src/media/MpvWidget.h
    src/media/MediaViewer.cpp
    src/media/MediaViewer.h
)
target_link_libraries(booru_widgets PUBLIC booru_core Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL Qt6::Concurrent PkgConfig::MPV)

add_executable(boorutagger src/main.cpp)
target_link_libraries(boorutagger PRIVATE booru_widgets)

foreach(target booru_core booru_widgets boorutagger)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_tags tests/test_tags.cpp)
    target_link_libraries(test_tags PRIVATE booru_core Qt6::Test)
    add_test(NAME tags COMMAND test_tags)
    add_executable(test_providers tests/test_providers.cpp)
    target_link_libraries(test_providers PRIVATE booru_core Qt6::Test)
    add_test(NAME providers COMMAND test_providers)
    add_executable(test_database tests/test_database.cpp)
    target_link_libraries(test_database PRIVATE booru_core Qt6::Test)
    add_test(NAME database COMMAND test_database)
    add_executable(test_sessions tests/test_sessions.cpp)
    target_link_libraries(test_sessions PRIVATE booru_core Qt6::Test)
    add_test(NAME sessions COMMAND test_sessions)
    add_executable(test_completion tests/test_completion.cpp)
    target_link_libraries(test_completion PRIVATE booru_core Qt6::Test)
    add_test(NAME completion COMMAND test_completion)
    add_executable(test_workflow tests/test_workflow.cpp)
    target_link_libraries(test_workflow PRIVATE booru_widgets Qt6::Test)
    add_test(NAME workflow COMMAND test_workflow -platform offscreen)
    set_tests_properties(workflow PROPERTIES TIMEOUT 45)
    add_executable(test_widgets tests/test_widgets.cpp)
    target_link_libraries(test_widgets PRIVATE booru_widgets Qt6::Test)
    add_test(NAME widgets COMMAND test_widgets -platform offscreen)
    add_executable(test_mpv tests/test_mpv.cpp)
    target_link_libraries(test_mpv PRIVATE booru_widgets Qt6::Test)
    add_test(NAME mpv COMMAND test_mpv)
    set_tests_properties(mpv PROPERTIES LABELS "rendering")
endif()

include(GNUInstallDirs)
install(TARGETS boorutagger RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/boorutagger.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES resources/boorutagger.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
