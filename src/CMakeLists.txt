qt_add_executable(NotepadNext
	WIN32
	MACOSX_BUNDLE
    update/PadUpdates.h update/PadUpdates.cpp
	ActionUtils.h
    LineDiff.h LineDiff.cpp
    NoteFormat.h NoteFormat.cpp
    GitStatus.h GitStatus.cpp
    CompareView.h CompareView.cpp
    TakeNotes.h TakeNotes.cpp
    ShellIntegration.h ShellIntegration.cpp
    FileAssociations.h FileAssociations.cpp
    FileAssociationsDialog.h FileAssociationsDialog.cpp
    BitPadFeatures.h BitPadFeatures.cpp
    WorkspaceFeatures.h WorkspaceFeatures.cpp
    UiIcons.h UiIcons.cpp
    ExternalTerminals.h ExternalTerminals.cpp
    OpenShellMenu.h OpenShellMenu.cpp
    TerminalPreferences.h TerminalPreferences.cpp
    LanguageMenu.h LanguageMenu.cpp
    DevOpsProfiles.h DevOpsProfiles.cpp
    MarkdownPreview.h MarkdownPreview.cpp
    ExplorerFileOperations.h ExplorerFileOperations.cpp
    ExplorerFileTypeIcon.h ExplorerFileTypeIcon.cpp
    ExplorerTreeView.h ExplorerTreeView.cpp
    FolderSearch.h FolderSearch.cpp
    FolderSearchDialog.h FolderSearchDialog.cpp
    NavigationPanels.h NavigationPanels.cpp
    FunctionExtractor.h FunctionExtractor.cpp
    ShortcutDialog.h ShortcutDialog.cpp
	ApplicationSettings.h
	ColorPickerDelegate.h
	ComboBoxDelegate.h
	Converter.h
	DebugManager.h
	DefaultDirectoryManager.h
	DockedEditor.h
	DockedEditorTitleBar.h

	EditorManager.h
	EditorPrintPreviewRenderer.h
	FileDialogHelpers.h
	FocusWatcher.h
	HtmlConverter.h
	ISearchResultsHandler.h
	LanguageStylesModel.h
	Macro.h
	MacroListModel.h
	MacroManager.h
	MacroRecorder.h
	MacroStep.h
	MacroStepTableModel.h
	NotepadNextApplication.h
	NppImporter.h
	RecentFilesListManager.h
	RecentFilesListMenuBuilder.h
	RtfConverter.h
	SearchResultData.h
	SearchResultHighlighterDelegate.h
	SearchResultsCollector.h
	SessionManager.h
	SpinBoxDelegate.h
	TranslationManager.h
	ZoomEventWatcher.h
	resources.qrc
	ApplicationSettings.cpp
	ColorPickerDelegate.cpp
	ComboBoxDelegate.cpp
	Converter.cpp
	DebugManager.cpp
	DefaultDirectoryManager.cpp
	DockedEditor.cpp

	EditorManager.cpp
	EditorPrintPreviewRenderer.cpp
	FileDialogHelpers.cpp
	HtmlConverter.cpp
	LanguageStylesModel.cpp
	Macro.cpp
	MacroListModel.cpp
	MacroManager.cpp
	MacroRecorder.cpp
	MacroStep.cpp
	MacroStepTableModel.cpp
	main.cpp
    AppSmokeTest.cpp AppSmokeTest.h
	NotepadNextApplication.cpp
	NppImporter.cpp
	RecentFilesListManager.cpp
	RecentFilesListMenuBuilder.cpp
	RtfConverter.cpp
	SearchResultHighlighterDelegate.cpp
	SearchResultsCollector.cpp
	SessionManager.cpp
	SpinBoxDelegate.cpp
	TranslationManager.cpp
	ZoomEventWatcher.cpp
	decorators/ApplicationDecorator.cpp
	decorators/ApplicationDecorator.h
	decorators/AutoCompletion.cpp
	decorators/AutoCompletion.h
	decorators/AutoIndentation.cpp
	decorators/AutoIndentation.h
	decorators/BetterMultiSelection.cpp
	decorators/BetterMultiSelection.h
	decorators/BookMarkDecorator.cpp
	decorators/BookMarkDecorator.h
	decorators/BraceMatch.cpp
	decorators/BraceMatch.h
	decorators/EditorConfigAppDecorator.cpp
	decorators/EditorConfigAppDecorator.h
	decorators/EditorDecorator.cpp
	decorators/EditorDecorator.h
	decorators/HighlightedScrollBar.cpp
	decorators/HighlightedScrollBar.h
	decorators/HTMLAutoCompleteDecorator.cpp
	decorators/HTMLAutoCompleteDecorator.h
	decorators/LineNumbers.cpp
	decorators/LineNumbers.h
	decorators/MarkerAppDecorator.cpp
	decorators/MarkerAppDecorator.h
	decorators/SmartHighlighter.cpp
	decorators/SmartHighlighter.h
	decorators/SurroundSelection.cpp
	decorators/SurroundSelection.h
	decorators/URLFinder.cpp
	decorators/URLFinder.h
	dialogs/ColumnEditorDialog.cpp
	dialogs/ColumnEditorDialog.h
	dialogs/ColumnEditorDialog.ui
	dialogs/FindReplaceDialog.cpp
	dialogs/FindReplaceDialog.h
	dialogs/FindReplaceDialog.ui
	dialogs/MacroEditorDialog.cpp
	dialogs/MacroEditorDialog.h
	dialogs/MacroEditorDialog.ui
	dialogs/MacroRunDialog.cpp
	dialogs/MacroRunDialog.h
	dialogs/MacroRunDialog.ui
	dialogs/MacroSaveDialog.cpp
	dialogs/MacroSaveDialog.h
	dialogs/MacroSaveDialog.ui
	dialogs/MainWindow.cpp
	dialogs/MainWindow.h
	dialogs/MainWindow.ui
	dialogs/PreferencesDialog.cpp
	dialogs/PreferencesDialog.h
	dialogs/PreferencesDialog.ui
	docks/DebugLogDock.cpp
	docks/DebugLogDock.h
	docks/DebugLogDock.ui
	docks/EditorInspectorDock.cpp
	docks/EditorInspectorDock.h
	docks/EditorInspectorDock.ui
	docks/FileListDock.cpp
	docks/FileListDock.h
	docks/FileListDock.ui
	docks/FolderAsWorkspaceDock.cpp
	docks/FolderAsWorkspaceDock.h
	docks/FolderAsWorkspaceDock.ui



	docks/LanguageInspectorDock.cpp
	docks/LanguageInspectorDock.h
	docks/LanguageInspectorDock.ui
	docks/LuaConsoleDock.cpp
	docks/LuaConsoleDock.h
	docks/LuaConsoleDock.ui
	docks/SearchResultsDock.cpp
	docks/SearchResultsDock.h
	docks/SearchResultsDock.ui
    widgets/EditorInfoStatusBar.cpp
	widgets/EditorInfoStatusBar.h
	widgets/FadingIndicator.cpp
	widgets/FadingIndicator.h
	widgets/QuickFindWidget.cpp
	widgets/QuickFindWidget.h
	widgets/QuickFindWidget.ui
	widgets/StatusLabel.cpp
	widgets/StatusLabel.h
	Sorter.h Sorter.cpp
	ScintillaSorter.h ScintillaSorter.cpp
	widgets/TabsQuickActionsBar.cpp
	widgets/TabsQuickActionsBar.h
	ShiftMiddleClickBlocker.h ShiftMiddleClickBlocker.cpp
	ShiftWheelToHorizontalScrollFilter.h ShiftWheelToHorizontalScrollFilter.cpp

)

if(WIN32)
    add_executable(BitPadCommand WIN32 CommandLauncherWindows.cpp)
    set_target_properties(BitPadCommand PROPERTIES OUTPUT_NAME 8p
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_executable(BitPadLegacyCommand WIN32 CommandLauncherWindows.cpp)
    set_target_properties(BitPadLegacyCommand PROPERTIES OUTPUT_NAME 8bitpad
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_library(BitPadFileIcons SHARED FileIconHandlerWindows.cpp
        FileIconHandlerWindows.h FileTypeIconMapWindows.h FileIconHandlerWindows.def)
    target_link_libraries(BitPadFileIcons PRIVATE ole32 uuid)
    set_target_properties(BitPadFileIcons PROPERTIES OUTPUT_NAME 8bitPadFileIcons
        PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(NotepadNext BitPadFileIcons)
endif()

set_target_properties(NotepadNext PROPERTIES
    OUTPUT_NAME 8bitCode
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/deploy/macos/info.plist
)

if(BUILD_TESTING)
    target_sources(NotepadNext PRIVATE ../tests/ThemeTest.cpp ../tests/ThemeTest.h
        ../tests/RecoveryTest.cpp ../tests/RecoveryTest.h
        ../tests/WorkspaceUiTest.cpp ../tests/WorkspaceUiTest.h)
    target_compile_definitions(NotepadNext PRIVATE BIT8PAD_TESTING)
endif()

file(GLOB TS_FILES
	${CMAKE_SOURCE_DIR}/i18n/*.ts
)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(NotepadNext
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
)


target_include_directories(NotepadNext
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decorators
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
        ${CMAKE_CURRENT_SOURCE_DIR}/docks
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
	configure_file(
		"${CMAKE_SOURCE_DIR}/deploy/windows/version.rc.in"
		"${CMAKE_BINARY_DIR}/version.rc"
		@ONLY
	)
	target_sources(NotepadNext PRIVATE "${CMAKE_BINARY_DIR}/version.rc")
	configure_file(
		"${CMAKE_SOURCE_DIR}/deploy/windows/filetypes.rc.in"
		"${CMAKE_BINARY_DIR}/filetypes.rc"
		@ONLY
	)
	configure_file(
		"${CMAKE_SOURCE_DIR}/icon/8bitPad.ico"
		"${CMAKE_BINARY_DIR}/8bitPad.ico"
		COPYONLY
	)
    # RC's ICON directive is not picked up by Ninja's C/C++ dependency scan.
    # An updated ICO must rebuild the EXE resource even when version.rc is unchanged.
    set_property(SOURCE "${CMAKE_BINARY_DIR}/version.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/8bitPad.ico")
    # File associations reference these stable negative resource IDs in the EXE.
    # Embedding keeps installed and portable icons independent of external paths.
    file(GLOB BIT8PAD_FILE_TYPE_ICONS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/icon/filetypes/*.ico")
    # Keep all ICON directives in one resource-compiler invocation: each RC
    # input otherwise starts its generated RT_ICON image IDs at 1 again.
    set_property(SOURCE "${CMAKE_BINARY_DIR}/version.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/filetypes.rc;${BIT8PAD_FILE_TYPE_ICONS}")
endif()

if(WIN32)
    target_link_libraries(NotepadNext PRIVATE ole32 uuid shlwapi)
endif()

target_link_libraries(NotepadNext
    PRIVATE
        bit8_updates
        EightBit::Editor
        Qt6::Concurrent
        Qt6::Svg
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::PrintSupport
        qtadvanceddocking-qt6
        SingleApplication::SingleApplication
        editorconfig-core-qt
        lua
        lexilla
        scintilla
        uchardet
)



target_compile_definitions(NotepadNext
    PRIVATE
        ADS_STATIC
        QAPPLICATION_CLASS=QApplication
        APP_VERSION="${PROJECT_VERSION}"
        APP_COPYRIGHT="${APP_COPYRIGHT}"
        APP_DISTRIBUTION="${APP_DISTRIBUTION}"
)
