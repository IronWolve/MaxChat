cmake_minimum_required(VERSION 3.24)

project(MaxChatCpp
    VERSION 0.1.0
    DESCRIPTION "Native C++/Qt port experiment for MaxChat"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Scripting engine (vendors Lua 5.4 under third_party/lua). ON by default now
# that the MinGW/Windows compile is verified; disable with -DMAXCHAT_LUA=OFF for
# a lean build without scripting.
option(MAXCHAT_LUA "Build the Lua scripting engine" ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test Multimedia
                                     MultimediaWidgets)
# Spellcheck uses a VENDORED Hunspell (third_party/hunspell) so the internal
# engine works on every platform (incl. Windows/MinGW) without a system package.
# Define the same HUNSPELL_* variables the downstream targets already consume.
add_library(maxchat_hunspell STATIC
    third_party/hunspell/affentry.cxx
    third_party/hunspell/affixmgr.cxx
    third_party/hunspell/csutil.cxx
    third_party/hunspell/filemgr.cxx
    third_party/hunspell/hashmgr.cxx
    third_party/hunspell/hunspell.cxx
    third_party/hunspell/hunzip.cxx
    third_party/hunspell/phonet.cxx
    third_party/hunspell/replist.cxx
    third_party/hunspell/suggestmgr.cxx
)
target_include_directories(maxchat_hunspell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell)
target_compile_definitions(maxchat_hunspell PUBLIC HUNSPELL_STATIC)
set(HUNSPELL_FOUND TRUE)
set(HUNSPELL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell)
set(HUNSPELL_LIBRARY_DIRS "")
set(HUNSPELL_CFLAGS_OTHER "")
set(HUNSPELL_LIBRARIES maxchat_hunspell)

qt_standard_project_setup()

if(MAXCHAT_LUA)
    # Lua is C; enable it only when scripting is requested so a C compiler isn't
    # required for the default build.
    enable_language(C)
    file(GLOB MAXCHAT_LUA_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c")
    add_library(maxchat_lua STATIC ${MAXCHAT_LUA_SOURCES})
    target_include_directories(maxchat_lua PUBLIC
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua")
    set_target_properties(maxchat_lua PROPERTIES C_STANDARD 99)
    # No LUA_USE_POSIX/DLL: the dlopen-based `require`/loadlib stays inert, which
    # suits the script sandbox. Build as portable ANSI C.
endif()

# The scripting engine is ALWAYS built so the app links one stable target; when
# MAXCHAT_LUA is off, LuaEngine.cpp compiles to inert stubs (no Lua linked).
add_library(maxchat_scripting STATIC
    src/scripting/LuaEngine.cpp
    src/scripting/LuaEngine.h
    src/scripting/ScriptHost.h
)
target_include_directories(maxchat_scripting PUBLIC
     "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(maxchat_scripting PUBLIC Qt6::Core)
if(MAXCHAT_LUA)
    target_compile_definitions(maxchat_scripting PUBLIC MAXCHAT_LUA=1)
    # maxchat_core for stripFormatting; maxchat_lua for the interpreter.
    target_link_libraries(maxchat_scripting PRIVATE maxchat_core maxchat_lua)
endif()

add_library(maxchat_core STATIC
    src/app/AppInfo.cpp
    src/app/AppInfo.h
    src/core/ChatBufferStore.cpp
    src/core/ChatBufferStore.h
    src/core/ChatLineFormatter.cpp
    src/core/ChatLineFormatter.h
    src/core/ChatLogStore.cpp
    src/core/ChatLogStore.h
    src/core/CommandAlias.cpp
    src/core/CommandAlias.h
    src/core/ConnectionPlan.cpp
    src/core/ConnectionPlan.h
    src/core/DefaultNetworks.cpp
    src/core/DefaultNetworks.h
    src/core/FloodGuard.cpp
    src/core/FloodGuard.h
    src/core/NetworkImport.cpp
    src/core/NetworkImport.h
    src/core/SettingsStore.cpp
    src/core/SettingsStore.h
    src/core/UrlDetector.cpp
    src/core/UrlDetector.h
    src/irc/CommandParser.cpp
    src/irc/CommandParser.h
    src/irc/IrcFormat.cpp
    src/irc/IrcFormat.h
    src/irc/IrcMessage.cpp
    src/irc/IrcMessage.h
    src/irc/McDataCodec.cpp
    src/irc/McDataCodec.h
    src/irc/IrcRedaction.cpp
    src/irc/IrcRedaction.h
    src/irc/ReconnectPlanner.cpp
    src/irc/ReconnectPlanner.h
    src/irc/IrcSession.cpp
    src/irc/IrcSession.h
)

target_include_directories(maxchat_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(maxchat_core
    PUBLIC
        Qt6::Core
)

add_library(maxchat_network STATIC
    src/irc/IrcConnection.cpp
    src/irc/IrcConnection.h
)

target_link_libraries(maxchat_network
    PUBLIC
        maxchat_core
        Qt6::Network
)

add_library(maxchat_upload STATIC
    src/upload/ImageUploader.h
    src/upload/ImgbbUploader.cpp
    src/upload/ImgbbUploader.h
    src/upload/ImgboxUploader.cpp
    src/upload/ImgboxUploader.h
    src/upload/ImgurUploader.cpp
    src/upload/ImgurUploader.h
    src/upload/PostimagesUploader.cpp
    src/upload/PostimagesUploader.h
    src/upload/ImageUploaderFactory.cpp
    src/upload/ImageUploaderFactory.h
)

target_include_directories(maxchat_upload PUBLIC src)
target_link_libraries(maxchat_upload PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
set_target_properties(maxchat_upload PROPERTIES AUTOMOC ON)

add_library(maxchat_services STATIC
    src/services/ImageFetcher.cpp
    src/services/ImageFetcher.h
    src/services/LinkPreviewClassifier.cpp
    src/services/LinkPreviewClassifier.h
    src/services/LinkPreviewPolicy.cpp
    src/services/LinkPreviewPolicy.h
    src/services/LinkPreviewRenderer.cpp
    src/services/LinkPreviewRenderer.h
    src/services/OpenGraphFetcher.cpp
    src/services/OpenGraphFetcher.h
    src/services/OpenGraphParser.cpp
    src/services/OpenGraphParser.h
)

target_include_directories(maxchat_services
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(maxchat_services
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
)

add_library(maxchat_spell STATIC
    src/spell/Speller.h
    src/spell/SpellerFactory.cpp
    src/spell/SpellcheckDictionaryCatalog.cpp
    src/spell/SpellcheckDictionaryCatalog.h
)

target_include_directories(maxchat_spell
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(maxchat_spell
    PUBLIC
        Qt6::Core
)

target_compile_definitions(maxchat_spell PRIVATE
    MAXCHAT_BUNDLED_DICTIONARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/dictionaries")

# Native OS speller (opt-in; Windows ISpellChecker). OFF by default so the
# normal build is never affected by the platform COM code. Only meaningful on
# Windows; the factory stub in SpellerFactory.cpp covers every other build.
option(MAXCHAT_OS_SPELL "Build the native OS spell engine (Windows ISpellChecker)" OFF)
if(MAXCHAT_OS_SPELL AND WIN32)
    target_sources(maxchat_spell PRIVATE src/spell/WindowsSpeller.cpp)
    target_compile_definitions(maxchat_spell PRIVATE MAXCHAT_OS_SPELL)
    target_link_libraries(maxchat_spell PRIVATE ole32)
endif()

if(HUNSPELL_FOUND)
    add_library(maxchat_spell_engine STATIC
        src/spell/HunspellSpellchecker.cpp
        src/spell/HunspellSpellchecker.h
    )

    target_include_directories(maxchat_spell_engine
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE
            ${HUNSPELL_INCLUDE_DIRS}
    )

    target_link_directories(maxchat_spell_engine
        PRIVATE
            ${HUNSPELL_LIBRARY_DIRS}
    )

    target_compile_options(maxchat_spell_engine
        PRIVATE
            ${HUNSPELL_CFLAGS_OTHER}
    )

    target_link_libraries(maxchat_spell_engine
        PUBLIC
            Qt6::Core
        PRIVATE
            ${HUNSPELL_LIBRARIES}
    )
endif()

qt_add_executable(maxchat-c
    src/main.cpp
    src/ui/AliasEditorDialog.cpp
    src/ui/AliasEditorDialog.h
    src/ui/AnsiRenderer.cpp
    src/ui/AnsiRenderer.h
    src/ui/BanListDialog.cpp
    src/ui/BanListDialog.h
    src/ui/ChannelListDialog.cpp
    src/ui/ChannelListDialog.h
    src/ui/ChannelModesDialog.cpp
    src/ui/ChannelModesDialog.h
    src/ui/AudioPlayerBar.cpp
    src/ui/AudioPlayerBar.h
    src/ui/ChatFindDialog.cpp
    src/ui/ChatFindDialog.h
    src/ui/ColorPickerDialog.cpp
    src/ui/ColorPickerDialog.h
    src/comic/ComicArt.cpp
    src/comic/ComicArt.h
    src/comic/ComicCharacter.cpp
    src/comic/ComicCharacter.h
    src/comic/ComicEmotion.cpp
    src/comic/ComicEmotion.h
    src/comic/ComicRenderer.cpp
    src/comic/ComicRenderer.h
    src/ui/ComicSettingsDialog.cpp
    src/ui/ComicSettingsDialog.h
    src/ui/ComicView.cpp
    src/ui/ComicView.h
    src/ui/DccManager.cpp
    src/ui/DccManager.h
    src/ui/DccTransfersDialog.cpp
    src/ui/DccTransfersDialog.h
    src/ui/ImageViewerDialog.cpp
    src/ui/ImageViewerDialog.h
    src/ui/MediaPlayerDialog.cpp
    src/ui/MediaPlayerDialog.h
    src/ui/FriendsNotifyDialog.cpp
    src/ui/FriendsNotifyDialog.h
    src/ui/GeometryPersist.cpp
    src/ui/GeometryPersist.h
    src/ui/IgnoreListDialog.cpp
    src/ui/IgnoreListDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/PreferencesDialog.cpp
    src/ui/PreferencesDialog.h
    src/ui/ThemeEditorDialog.cpp
    src/ui/ThemeEditorDialog.h
    src/ui/QuickConnectDialog.cpp
    src/ui/QuickConnectDialog.h
    src/ui/RawLogDialog.cpp
    src/ui/RawLogDialog.h
    src/ui/ServerListDialog.cpp
    src/ui/ServerListDialog.h
    src/ui/ScriptTerminalDialog.cpp
    src/ui/ScriptTerminalDialog.h
    src/ui/ScriptTerminalManager.cpp
    src/ui/ScriptTerminalManager.h
    src/ui/TerminalFrame.cpp
    src/ui/TerminalFrame.h
    src/ui/ShortcutEditorDialog.cpp
    src/ui/ShortcutEditorDialog.h
    src/ui/SpellcheckHighlighter.cpp
    src/ui/SpellcheckHighlighter.h
    src/ui/SpellTextEdit.cpp
    src/ui/SpellTextEdit.h
    src/ui/ThemeCatalog.cpp
    src/ui/ThemeCatalog.h
    src/ui/UrlListDialog.cpp
    src/ui/UrlListDialog.h
    src/ui/AppIcon.cpp
    src/ui/AppIcon.h
    src/ui/ToastWidget.cpp
    src/ui/ToastWidget.h
    src/ui/Notifier.cpp
    src/ui/Notifier.h
    src/ui/SoundPlayer.cpp
    src/ui/SoundPlayer.h
    src/ui/SystemInfo.cpp
    src/ui/SystemInfo.h
    src/ui/TerminalProfile.cpp
    src/ui/TerminalProfile.h
    resources/maxchat.qrc
)

set_target_properties(maxchat-c
    PROPERTIES
        WIN32_EXECUTABLE $<PLATFORM_ID:Windows>
)

target_link_libraries(maxchat-c
    PRIVATE
        maxchat_core
        maxchat_network
        maxchat_services
        maxchat_upload
        maxchat_spell
        maxchat_scripting
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
)

if(HUNSPELL_FOUND)
    target_link_libraries(maxchat-c
        PRIVATE
            maxchat_spell_engine
    )
    target_compile_definitions(maxchat-c
        PRIVATE
            MAXCHAT_WITH_HUNSPELL=1
    )
endif()

enable_testing()

qt_add_executable(app_info_test
    tests/unit/app_info_test.cpp
)

target_link_libraries(app_info_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME app_info COMMAND app_info_test)

qt_add_executable(ansi_renderer_test
    tests/unit/ansi_renderer_test.cpp
    src/ui/AnsiRenderer.cpp
    src/ui/AnsiRenderer.h
)

target_include_directories(ansi_renderer_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ansi_renderer_test
    PRIVATE
        Qt6::Core
        Qt6::Test
)

add_test(NAME ansi_renderer COMMAND ansi_renderer_test)

qt_add_executable(terminal_frame_test
    tests/unit/terminal_frame_test.cpp
    src/ui/TerminalFrame.cpp
    src/ui/TerminalFrame.h
)

target_include_directories(terminal_frame_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(terminal_frame_test
    PRIVATE
        Qt6::Core
        Qt6::Test
)

add_test(NAME terminal_frame COMMAND terminal_frame_test)

qt_add_executable(terminal_profile_test
    tests/unit/terminal_profile_test.cpp
    src/ui/TerminalProfile.cpp
    src/ui/TerminalProfile.h
)

target_include_directories(terminal_profile_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(terminal_profile_test
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)

add_test(NAME terminal_profile COMMAND terminal_profile_test)

qt_add_executable(script_terminal_manager_test
    tests/unit/script_terminal_manager_test.cpp
    src/ui/ScriptTerminalManager.cpp
    src/ui/ScriptTerminalManager.h
    src/ui/ScriptTerminalDialog.cpp
    src/ui/ScriptTerminalDialog.h
    src/ui/AnsiRenderer.cpp
    src/ui/AnsiRenderer.h
    src/ui/TerminalFrame.cpp
    src/ui/TerminalFrame.h
    src/ui/TerminalProfile.cpp
    src/ui/TerminalProfile.h
)

target_include_directories(script_terminal_manager_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(script_terminal_manager_test
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME script_terminal_manager COMMAND script_terminal_manager_test)

qt_add_executable(irc_message_test
    tests/unit/irc_message_test.cpp
)

target_link_libraries(irc_message_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME irc_message COMMAND irc_message_test)

qt_add_executable(irc_format_test
    tests/unit/irc_format_test.cpp
)

target_link_libraries(irc_format_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME irc_format COMMAND irc_format_test)

qt_add_executable(irc_redaction_test
    tests/unit/irc_redaction_test.cpp
)

target_link_libraries(irc_redaction_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME irc_redaction COMMAND irc_redaction_test)

qt_add_executable(reconnect_planner_test
    tests/unit/reconnect_planner_test.cpp
)

target_link_libraries(reconnect_planner_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME reconnect_planner COMMAND reconnect_planner_test)

qt_add_executable(command_parser_test
    tests/unit/command_parser_test.cpp
)

target_link_libraries(command_parser_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME command_parser COMMAND command_parser_test)

qt_add_executable(command_alias_test
    tests/unit/command_alias_test.cpp
)

target_link_libraries(command_alias_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME command_alias COMMAND command_alias_test)

qt_add_executable(flood_guard_test
    tests/unit/flood_guard_test.cpp
)

target_link_libraries(flood_guard_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME flood_guard COMMAND flood_guard_test)

qt_add_executable(chat_line_formatter_test
    tests/unit/chat_line_formatter_test.cpp
)

target_link_libraries(chat_line_formatter_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME chat_line_formatter COMMAND chat_line_formatter_test)

qt_add_executable(open_graph_parser_test
    tests/unit/open_graph_parser_test.cpp
)

target_link_libraries(open_graph_parser_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME open_graph_parser COMMAND open_graph_parser_test)

qt_add_executable(open_graph_fetcher_test
    tests/unit/open_graph_fetcher_test.cpp
)

target_link_libraries(open_graph_fetcher_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME open_graph_fetcher COMMAND open_graph_fetcher_test)

qt_add_executable(image_fetcher_test
    tests/unit/image_fetcher_test.cpp
)

target_link_libraries(image_fetcher_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME image_fetcher COMMAND image_fetcher_test)

qt_add_executable(image_uploader_test
    tests/unit/image_uploader_test.cpp
)

target_link_libraries(image_uploader_test
    PRIVATE
        maxchat_upload
        Qt6::Network
        Qt6::Test
)

add_test(NAME image_uploader COMMAND image_uploader_test)

qt_add_executable(link_preview_classifier_test
    tests/unit/link_preview_classifier_test.cpp
)

target_link_libraries(link_preview_classifier_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME link_preview_classifier COMMAND link_preview_classifier_test)

qt_add_executable(link_preview_policy_test
    tests/unit/link_preview_policy_test.cpp
)

target_link_libraries(link_preview_policy_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME link_preview_policy COMMAND link_preview_policy_test)

qt_add_executable(link_preview_renderer_test
    tests/unit/link_preview_renderer_test.cpp
)

target_link_libraries(link_preview_renderer_test
    PRIVATE
        maxchat_services
        Qt6::Test
)

add_test(NAME link_preview_renderer COMMAND link_preview_renderer_test)

qt_add_executable(spellcheck_dictionary_catalog_test
    tests/unit/spellcheck_dictionary_catalog_test.cpp
)

target_link_libraries(spellcheck_dictionary_catalog_test
    PRIVATE
        maxchat_spell
        Qt6::Test
)

add_test(NAME spellcheck_dictionary_catalog COMMAND spellcheck_dictionary_catalog_test)

qt_add_executable(spellcheck_highlighter_test
    tests/unit/spellcheck_highlighter_test.cpp
    src/ui/SpellcheckHighlighter.cpp
    src/ui/SpellcheckHighlighter.h
)

target_include_directories(spellcheck_highlighter_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(spellcheck_highlighter_test
    PRIVATE
        Qt6::Gui
        Qt6::Test
)

add_test(NAME spellcheck_highlighter COMMAND spellcheck_highlighter_test)

if(HUNSPELL_FOUND)
    qt_add_executable(hunspell_spellchecker_test
        tests/unit/hunspell_spellchecker_test.cpp
    )

    target_link_libraries(hunspell_spellchecker_test
        PRIVATE
            maxchat_spell_engine
            Qt6::Test
    )

    target_compile_definitions(hunspell_spellchecker_test PRIVATE
        MAXCHAT_TEST_DICTIONARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/dictionaries")

    add_test(NAME hunspell_spellchecker COMMAND hunspell_spellchecker_test)
endif()

qt_add_executable(connection_plan_test
    tests/unit/connection_plan_test.cpp
)

target_link_libraries(connection_plan_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME connection_plan COMMAND connection_plan_test)

qt_add_executable(irc_session_test
    tests/unit/irc_session_test.cpp
)

target_link_libraries(irc_session_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME irc_session COMMAND irc_session_test)

qt_add_executable(dcc_manager_test
    tests/unit/dcc_manager_test.cpp
    src/ui/DccManager.cpp
    src/ui/DccManager.h
)

target_include_directories(dcc_manager_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dcc_manager_test
    PRIVATE
        maxchat_core
        Qt6::Network
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME dcc_manager COMMAND dcc_manager_test)
set_tests_properties(dcc_manager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(comic_art_test
    tests/unit/comic_art_test.cpp
    src/comic/ComicArt.cpp
    src/comic/ComicArt.h
)

target_include_directories(comic_art_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(comic_art_test
    PRIVATE
        Qt6::Gui
        Qt6::Test
)

add_test(NAME comic_art COMMAND comic_art_test)

qt_add_executable(comic_emotion_test
    tests/unit/comic_emotion_test.cpp
    src/comic/ComicEmotion.cpp
    src/comic/ComicEmotion.h
)

target_include_directories(comic_emotion_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(comic_emotion_test
    PRIVATE
        Qt6::Test
)

add_test(NAME comic_emotion COMMAND comic_emotion_test)

qt_add_executable(default_networks_test
    tests/unit/default_networks_test.cpp
)

target_link_libraries(default_networks_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME default_networks COMMAND default_networks_test)

qt_add_executable(network_import_test
    tests/unit/network_import_test.cpp
)

target_link_libraries(network_import_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME network_import COMMAND network_import_test)

qt_add_executable(settings_store_test
    tests/unit/settings_store_test.cpp
)

target_link_libraries(settings_store_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME settings_store COMMAND settings_store_test)

qt_add_executable(url_detector_test
    tests/unit/url_detector_test.cpp
)

target_link_libraries(url_detector_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME url_detector COMMAND url_detector_test)

qt_add_executable(sound_player_test
    tests/unit/sound_player_test.cpp
    src/ui/SoundPlayer.cpp
    src/ui/SoundPlayer.h
)

target_include_directories(sound_player_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sound_player_test
    PRIVATE
        Qt6::Test
        Qt6::Multimedia
)

add_test(NAME sound_player COMMAND sound_player_test)

qt_add_executable(system_info_test
    tests/unit/system_info_test.cpp
    src/ui/SystemInfo.cpp
    src/ui/SystemInfo.h
)

target_include_directories(system_info_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(system_info_test
    PRIVATE
        Qt6::Test
        Qt6::Gui
)

add_test(NAME system_info COMMAND system_info_test)

qt_add_executable(chat_log_store_test
    tests/unit/chat_log_store_test.cpp
)

target_link_libraries(chat_log_store_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME chat_log_store COMMAND chat_log_store_test)

qt_add_executable(chat_buffer_store_test
    tests/unit/chat_buffer_store_test.cpp
)

target_link_libraries(chat_buffer_store_test
    PRIVATE
        maxchat_core
        Qt6::Test
)

add_test(NAME chat_buffer_store COMMAND chat_buffer_store_test)

qt_add_executable(chat_find_dialog_test
    tests/unit/chat_find_dialog_test.cpp
    src/ui/ChatFindDialog.cpp
    src/ui/ChatFindDialog.h
)

target_include_directories(chat_find_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(chat_find_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME chat_find_dialog COMMAND chat_find_dialog_test)
set_tests_properties(chat_find_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(alias_editor_dialog_test
    tests/unit/alias_editor_dialog_test.cpp
    src/ui/AliasEditorDialog.cpp
    src/ui/AliasEditorDialog.h
)

target_include_directories(alias_editor_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(alias_editor_dialog_test
    PRIVATE
        maxchat_core
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME alias_editor_dialog COMMAND alias_editor_dialog_test)
set_tests_properties(alias_editor_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(channel_modes_dialog_test
    tests/unit/channel_modes_dialog_test.cpp
    src/ui/ChannelModesDialog.cpp
    src/ui/ChannelModesDialog.h
)

target_include_directories(channel_modes_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(channel_modes_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME channel_modes_dialog COMMAND channel_modes_dialog_test)
set_tests_properties(channel_modes_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(channel_list_dialog_test
    tests/unit/channel_list_dialog_test.cpp
    src/ui/ChannelListDialog.cpp
    src/ui/ChannelListDialog.h
)

target_include_directories(channel_list_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(channel_list_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME channel_list_dialog COMMAND channel_list_dialog_test)
set_tests_properties(channel_list_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ban_list_dialog_test
    tests/unit/ban_list_dialog_test.cpp
    src/ui/BanListDialog.cpp
    src/ui/BanListDialog.h
)

target_include_directories(ban_list_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ban_list_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME ban_list_dialog COMMAND ban_list_dialog_test)
set_tests_properties(ban_list_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ignore_list_dialog_test
    tests/unit/ignore_list_dialog_test.cpp
    src/ui/IgnoreListDialog.cpp
    src/ui/IgnoreListDialog.h
)

target_include_directories(ignore_list_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ignore_list_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME ignore_list_dialog COMMAND ignore_list_dialog_test)
set_tests_properties(ignore_list_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(friends_notify_dialog_test
    tests/unit/friends_notify_dialog_test.cpp
    src/ui/FriendsNotifyDialog.cpp
    src/ui/FriendsNotifyDialog.h
)

target_include_directories(friends_notify_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(friends_notify_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME friends_notify_dialog COMMAND friends_notify_dialog_test)
set_tests_properties(friends_notify_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(server_list_dialog_test
    tests/unit/server_list_dialog_test.cpp
    src/ui/ServerListDialog.cpp
    src/ui/ServerListDialog.h
)

target_link_libraries(server_list_dialog_test
    PRIVATE
        maxchat_core
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME server_list_dialog COMMAND server_list_dialog_test)
set_tests_properties(server_list_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(theme_catalog_test
    tests/unit/theme_catalog_test.cpp
    src/ui/ThemeCatalog.cpp
    src/ui/ThemeCatalog.h
)

target_include_directories(theme_catalog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(theme_catalog_test
    PRIVATE
        Qt6::Gui
        Qt6::Test
)

add_test(NAME theme_catalog COMMAND theme_catalog_test)
set_tests_properties(theme_catalog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(preferences_dialog_test
    tests/unit/preferences_dialog_test.cpp
    src/ui/PreferencesDialog.cpp
    src/ui/PreferencesDialog.h
    src/ui/ThemeEditorDialog.cpp
    src/ui/ThemeEditorDialog.h
    src/ui/ThemeCatalog.cpp
    src/ui/ThemeCatalog.h
    src/ui/AppIcon.cpp
    src/ui/AppIcon.h
)

target_link_libraries(preferences_dialog_test
    PRIVATE
        maxchat_core
        maxchat_spell
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME preferences_dialog COMMAND preferences_dialog_test)
set_tests_properties(preferences_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(main_window_link_preview_test
    tests/unit/main_window_link_preview_test.cpp
    src/ui/AliasEditorDialog.cpp
    src/ui/AliasEditorDialog.h
    src/ui/BanListDialog.cpp
    src/ui/BanListDialog.h
    src/ui/ChannelListDialog.cpp
    src/ui/ChannelListDialog.h
    src/ui/ChannelModesDialog.cpp
    src/ui/ChannelModesDialog.h
    src/ui/AudioPlayerBar.cpp
    src/ui/AudioPlayerBar.h
    src/ui/ChatFindDialog.cpp
    src/ui/ChatFindDialog.h
    src/ui/ColorPickerDialog.cpp
    src/ui/ColorPickerDialog.h
    src/comic/ComicArt.cpp
    src/comic/ComicArt.h
    src/comic/ComicCharacter.cpp
    src/comic/ComicCharacter.h
    src/comic/ComicEmotion.cpp
    src/comic/ComicEmotion.h
    src/comic/ComicRenderer.cpp
    src/comic/ComicRenderer.h
    src/ui/ComicSettingsDialog.cpp
    src/ui/ComicSettingsDialog.h
    src/ui/ComicView.cpp
    src/ui/ComicView.h
    src/ui/DccManager.cpp
    src/ui/DccManager.h
    src/ui/DccTransfersDialog.cpp
    src/ui/DccTransfersDialog.h
    src/ui/ImageViewerDialog.cpp
    src/ui/ImageViewerDialog.h
    src/ui/MediaPlayerDialog.cpp
    src/ui/MediaPlayerDialog.h
    src/ui/FriendsNotifyDialog.cpp
    src/ui/FriendsNotifyDialog.h
    src/ui/GeometryPersist.cpp
    src/ui/GeometryPersist.h
    src/ui/IgnoreListDialog.cpp
    src/ui/IgnoreListDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/PreferencesDialog.cpp
    src/ui/PreferencesDialog.h
    src/ui/ThemeEditorDialog.cpp
    src/ui/ThemeEditorDialog.h
    src/ui/QuickConnectDialog.cpp
    src/ui/QuickConnectDialog.h
    src/ui/RawLogDialog.cpp
    src/ui/RawLogDialog.h
    src/ui/AnsiRenderer.cpp
    src/ui/AnsiRenderer.h
    src/ui/ServerListDialog.cpp
    src/ui/ServerListDialog.h
    src/ui/ScriptTerminalDialog.cpp
    src/ui/ScriptTerminalDialog.h
    src/ui/ScriptTerminalManager.cpp
    src/ui/ScriptTerminalManager.h
    src/ui/TerminalFrame.cpp
    src/ui/TerminalFrame.h
    src/ui/ShortcutEditorDialog.cpp
    src/ui/ShortcutEditorDialog.h
    src/ui/SpellcheckHighlighter.cpp
    src/ui/SpellcheckHighlighter.h
    src/ui/SpellTextEdit.cpp
    src/ui/SpellTextEdit.h
    src/ui/ThemeCatalog.cpp
    src/ui/ThemeCatalog.h
    src/ui/TerminalProfile.cpp
    src/ui/TerminalProfile.h
    src/ui/UrlListDialog.cpp
    src/ui/UrlListDialog.h
    src/ui/AppIcon.cpp
    src/ui/AppIcon.h
    src/ui/ToastWidget.cpp
    src/ui/ToastWidget.h
    src/ui/Notifier.cpp
    src/ui/Notifier.h
    src/ui/SoundPlayer.cpp
    src/ui/SoundPlayer.h
    src/ui/SystemInfo.cpp
    src/ui/SystemInfo.h
    resources/maxchat.qrc
)

target_link_libraries(main_window_link_preview_test
    PRIVATE
        maxchat_core
        maxchat_network
        maxchat_services
        maxchat_upload
        maxchat_spell
        maxchat_scripting
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        Qt6::Test
)

if(HUNSPELL_FOUND)
    target_link_libraries(main_window_link_preview_test
        PRIVATE
            maxchat_spell_engine
    )
    target_compile_definitions(main_window_link_preview_test
        PRIVATE
            MAXCHAT_WITH_HUNSPELL=1
    )
endif()

add_test(NAME main_window_link_preview COMMAND main_window_link_preview_test)
set_tests_properties(main_window_link_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(quick_connect_dialog_test
    tests/unit/quick_connect_dialog_test.cpp
    src/ui/QuickConnectDialog.cpp
    src/ui/QuickConnectDialog.h
)

target_link_libraries(quick_connect_dialog_test
    PRIVATE
        maxchat_core
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME quick_connect_dialog COMMAND quick_connect_dialog_test)
set_tests_properties(quick_connect_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(raw_log_dialog_test
    tests/unit/raw_log_dialog_test.cpp
    src/ui/RawLogDialog.cpp
    src/ui/RawLogDialog.h
)

target_include_directories(raw_log_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(raw_log_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME raw_log_dialog COMMAND raw_log_dialog_test)
set_tests_properties(raw_log_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(url_list_dialog_test
    tests/unit/url_list_dialog_test.cpp
    src/ui/UrlListDialog.cpp
    src/ui/UrlListDialog.h
)

target_include_directories(url_list_dialog_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(url_list_dialog_test
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME url_list_dialog COMMAND url_list_dialog_test)
set_tests_properties(url_list_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(irc_connection_test
    tests/unit/irc_connection_test.cpp
)

target_link_libraries(irc_connection_test
    PRIVATE
        maxchat_network
        Qt6::Test
)

add_test(NAME irc_connection COMMAND irc_connection_test)

if(MAXCHAT_LUA)
    qt_add_executable(lua_smoke_test
        tests/unit/lua_smoke_test.cpp
    )

    target_link_libraries(lua_smoke_test
        PRIVATE
            maxchat_lua
            Qt6::Test
    )

    add_test(NAME lua_smoke COMMAND lua_smoke_test)

    qt_add_executable(lua_engine_test
        tests/unit/lua_engine_test.cpp
        src/ui/TerminalFrame.cpp
        src/ui/TerminalFrame.h
    )

    target_include_directories(lua_engine_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(lua_engine_test
        PRIVATE
            maxchat_scripting
            Qt6::Test
    )
    target_compile_definitions(lua_engine_test PRIVATE
        MAXCHAT_EXAMPLE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/scripts")

    add_test(NAME lua_engine COMMAND lua_engine_test)
endif()
