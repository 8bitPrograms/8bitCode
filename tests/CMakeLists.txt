qt_add_executable(EditorCoreTest EditorCoreTest.cpp)
qt_add_executable(UpdateClientTest UpdateClientTest.cpp)
target_link_libraries(UpdateClientTest PRIVATE bit8_updates Qt6::Test)
set_target_properties(UpdateClientTest PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME UpdateClient COMMAND UpdateClientTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/updates.xml,junitxml")
set_tests_properties(UpdateClient PROPERTIES TIMEOUT 30)
target_link_libraries(EditorCoreTest PRIVATE EightBit::Editor Qt6::Test)
add_test(NAME EditorCore COMMAND EditorCoreTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/editor-core.xml,junitxml")
set_tests_properties(EditorCore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(VimModeTest VimModeTest.cpp ../src/VimMode.cpp ../src/VimMode.h)
target_include_directories(VimModeTest PRIVATE ../src)
target_link_libraries(VimModeTest PRIVATE EightBit::Editor Qt6::Test)
add_test(NAME VimMode COMMAND VimModeTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/vim-mode.xml,junitxml")
set_tests_properties(VimMode PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(FolderSearchTest FolderSearchTest.cpp ../src/FolderSearch.cpp ../src/FolderSearch.h)
target_include_directories(FolderSearchTest PRIVATE ../src)
target_link_libraries(FolderSearchTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME FolderSearch COMMAND FolderSearchTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/folder-search.xml,junitxml")
set_tests_properties(FolderSearch PROPERTIES TIMEOUT 120)

qt_add_executable(NavigationTest NavigationTest.cpp ../src/FunctionExtractor.cpp)
target_include_directories(NavigationTest PRIVATE ../src)
target_link_libraries(NavigationTest PRIVATE Qt6::Test)
add_test(NAME Navigation COMMAND NavigationTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/navigation.xml,junitxml")
set_tests_properties(Navigation PROPERTIES TIMEOUT 120)

string(REPLACE "\\" "/" BIT8PAD_SOURCE_FWD "${CMAKE_SOURCE_DIR}")
qt_add_executable(EditorExtrasTest
    EditorExtrasTest.cpp
    ../src/LineDiff.cpp ../src/NoteFormat.cpp ../src/GitStatus.cpp ../src/Readability.cpp
    ../src/FileAssociations.cpp
)
target_include_directories(EditorExtrasTest PRIVATE ../src)
target_link_libraries(EditorExtrasTest PRIVATE Qt6::Widgets Qt6::Test)
if(WIN32)
    target_link_libraries(EditorExtrasTest PRIVATE ole32 uuid shlwapi)
endif()
target_compile_definitions(EditorExtrasTest PRIVATE "BIT8PAD_SOURCE_DIR=\"${BIT8PAD_SOURCE_FWD}\"")
add_test(NAME EditorExtras COMMAND EditorExtrasTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/editor-extras.xml,junitxml")
set_tests_properties(EditorExtras PROPERTIES TIMEOUT 60)

set_target_properties(EditorCoreTest VimModeTest FolderSearchTest NavigationTest EditorExtrasTest
    PROPERTIES WIN32_EXECUTABLE FALSE)

add_test(NAME ApplicationSmoke COMMAND NotepadNext --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/smoke")
set_tests_properties(ApplicationSmoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)

qt_add_executable(MarkdownPreviewTest MarkdownPreviewTest.cpp
    ../src/MarkdownRenderer.cpp ../src/MarkdownRenderer.h
    ../src/MarkdownPreview.cpp ../src/MarkdownPreview.h)
target_include_directories(MarkdownPreviewTest PRIVATE ../src)
target_link_libraries(MarkdownPreviewTest PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME MarkdownPreview COMMAND MarkdownPreviewTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/markdown-preview.xml,junitxml")
set_tests_properties(MarkdownPreview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(LanguageProfilesTest LanguageProfilesTest.cpp
    ../src/LanguageMenu.cpp ../src/LanguageMenu.h
    ../src/DevOpsProfiles.cpp ../src/DevOpsProfiles.h)
target_include_directories(LanguageProfilesTest PRIVATE ../src)
target_link_libraries(LanguageProfilesTest PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME LanguageProfiles COMMAND LanguageProfilesTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/language-profiles.xml,junitxml")
set_tests_properties(LanguageProfiles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(ExplorerFileOperationsTest ExplorerFileOperationsTest.cpp ../src/ExplorerFileOperations.cpp)
target_include_directories(ExplorerFileOperationsTest PRIVATE ../src)
target_link_libraries(ExplorerFileOperationsTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME ExplorerFileOperations COMMAND ExplorerFileOperationsTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/explorer-file-operations.xml,junitxml")
set_tests_properties(ExplorerFileOperations PROPERTIES TIMEOUT 60)
set_target_properties(MarkdownPreviewTest LanguageProfilesTest ExplorerFileOperationsTest
    PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(UiIconsTest UiIconsTest.cpp ../src/UiIcons.cpp ../src/ExplorerFileTypeIcon.cpp)
target_include_directories(UiIconsTest PRIVATE ../src)
target_link_libraries(UiIconsTest PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(UiIconsTest PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME UiIcons COMMAND UiIconsTest -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/ui-icons.xml,junitxml")
set_tests_properties(UiIcons PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(ExternalTerminalsTest ExternalTerminalsTest.cpp ../src/ExternalTerminals.cpp)
target_include_directories(ExternalTerminalsTest PRIVATE ../src)
target_link_libraries(ExternalTerminalsTest PRIVATE Qt6::Core Qt6::Test)

qt_add_executable(OpenShellMenuTest OpenShellMenuTest.cpp ../src/OpenShellMenu.cpp
    ../src/ExternalTerminals.cpp ../src/UiIcons.cpp ../src/ExplorerFileTypeIcon.cpp)
target_include_directories(OpenShellMenuTest PRIVATE ../src)
target_link_libraries(OpenShellMenuTest PRIVATE Qt6::Widgets Qt6::Test)

qt_add_executable(TerminalPreferencesTest TerminalPreferencesTest.cpp ../src/TerminalPreferences.cpp
    ../src/TerminalPreferences.h ../src/ExternalTerminals.cpp ../src/UiIcons.cpp ../src/ExplorerFileTypeIcon.cpp)
target_include_directories(TerminalPreferencesTest PRIVATE ../src)
target_link_libraries(TerminalPreferencesTest PRIVATE Qt6::Widgets Qt6::Test)

foreach(terminal_test ExternalTerminalsTest OpenShellMenuTest TerminalPreferencesTest)
    if(WIN32)
        target_link_libraries(${terminal_test} PRIVATE ole32 uuid)
    endif()
    set_target_properties(${terminal_test} PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME ${terminal_test} COMMAND ${terminal_test} -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/${terminal_test}.xml,junitxml")
    set_tests_properties(${terminal_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endforeach()

qt_add_executable(FileAssociationsTest FileAssociationsTest.cpp ../src/FileAssociations.cpp)
target_include_directories(FileAssociationsTest PRIVATE ../src)
target_link_libraries(FileAssociationsTest PRIVATE Qt6::Gui Qt6::Test)

qt_add_executable(FileAssociationsDialogTest FileAssociationsDialogTest.cpp
    ../src/FileAssociationsDialog.h ../src/FileAssociationsDialog.cpp ../src/FileAssociations.cpp
    ../src/UiIcons.cpp ../src/ExplorerFileTypeIcon.cpp)
target_include_directories(FileAssociationsDialogTest PRIVATE ../src)
target_link_libraries(FileAssociationsDialogTest PRIVATE Qt6::Widgets Qt6::Test)

foreach(association_test FileAssociationsTest FileAssociationsDialogTest)
    if(WIN32)
        target_link_libraries(${association_test} PRIVATE shlwapi)
    endif()
    set_target_properties(${association_test} PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME ${association_test} COMMAND ${association_test} -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/${association_test}.xml,junitxml")
    set_tests_properties(${association_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endforeach()

if(WIN32)
    qt_add_executable(WindowsFileIconHandlerTest WindowsFileIconHandlerTest.cpp)
    target_include_directories(WindowsFileIconHandlerTest PRIVATE ../src)
    target_link_libraries(WindowsFileIconHandlerTest PRIVATE Qt6::Core Qt6::Test ole32 uuid)
    target_compile_definitions(WindowsFileIconHandlerTest PRIVATE
        "BIT8PAD_ICON_HANDLER_DLL=\"$<TARGET_FILE:BitPadFileIcons>\"")
    set_target_properties(WindowsFileIconHandlerTest PROPERTIES WIN32_EXECUTABLE FALSE)
    add_dependencies(WindowsFileIconHandlerTest BitPadFileIcons)
    add_test(NAME WindowsFileIconHandler COMMAND WindowsFileIconHandlerTest
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/windows-file-icon-handler.xml,junitxml")
    set_tests_properties(WindowsFileIconHandler PROPERTIES TIMEOUT 30)

    qt_add_executable(WindowsFileIconResourceTest WindowsFileIconResourceTest.cpp)
    target_include_directories(WindowsFileIconResourceTest PRIVATE ../src)
    target_link_libraries(WindowsFileIconResourceTest PRIVATE Qt6::Gui Qt6::Test shell32 user32 gdi32)
    target_compile_definitions(WindowsFileIconResourceTest PRIVATE
        "BIT8PAD_TEST_EXECUTABLE=\"$<TARGET_FILE:NotepadNext>\""
        "BIT8PAD_SOURCE_DIR=\"${BIT8PAD_SOURCE_FWD}\"")
    set_target_properties(WindowsFileIconResourceTest PROPERTIES WIN32_EXECUTABLE FALSE)
    add_dependencies(WindowsFileIconResourceTest NotepadNext)
    add_test(NAME WindowsFileIconResources COMMAND WindowsFileIconResourceTest
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/windows-file-icons.xml,junitxml")
    set_tests_properties(WindowsFileIconResources PROPERTIES TIMEOUT 30)

    qt_add_executable(CommandLauncherTest CommandLauncherTest.cpp)
    target_link_libraries(CommandLauncherTest PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(CommandLauncherTest PROPERTIES WIN32_EXECUTABLE FALSE)
    add_dependencies(CommandLauncherTest BitPadCommand)
    add_test(NAME CommandLauncher COMMAND CommandLauncherTest
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/command-launcher.xml,junitxml")
    set_tests_properties(CommandLauncher PROPERTIES TIMEOUT 30)
endif()
