# SPDX-License-Identifier: GPL-3.0-or-later
# EightBit::Editor: the product-independent editor shared by 8bitPad and 8bitNotes.
# Scintilla/Lexilla editing, document I/O (encoding, BOM, EOL), recovery copies,
# Lua language profiles and editor themes. Product settings, windows, packaging
# and updates belong to the executable that links this target.

foreach(dependency lua scintilla lexilla uchardet)
    if(NOT TARGET ${dependency})
        message(FATAL_ERROR "EightBit::Editor needs the '${dependency}' target from thirdparty/")
    endif()
endforeach()

qt_add_library(EightBitEditor STATIC
    ../VimMode.h ../VimMode.cpp
    ../ThemeManager.h ../ThemeManager.cpp
    ../Readability.h ../Readability.cpp
    ../MarkdownRenderer.h ../MarkdownRenderer.cpp
    ByteArrayUtils.h
    Finder.h Finder.cpp
    IFaceTable.h IFaceTable.cpp
    IFaceTableMixer.h IFaceTableMixer.cpp
    LuaExtension.h LuaExtension.cpp
    LuaState.h LuaState.cpp
    RangeAllocator.h RangeAllocator.cpp
    SciIFaceTable.h SciIFaceTable.cpp
    ScintillaCommenter.h ScintillaCommenter.cpp
    ScintillaEnums.h
    ScintillaNext.h ScintillaNext.cpp
    SelectionTracker.h SelectionTracker.cpp
    UndoAction.h UndoAction.cpp
)
add_library(EightBit::Editor ALIAS EightBitEditor)
set_target_properties(EightBitEditor PROPERTIES OUTPUT_NAME 8bitEditor)

target_include_directories(EightBitEditor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(EightBitEditor
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Core5Compat
        lua
        lexilla
        scintilla
        uchardet
)

# The regex callback must reach the final binary before libscintilla. Qt
# resource collections are interface sources, compiled by AUTORCC in each
# final consumer. Forwarding AUTORCC-generated objects via TARGET_OBJECTS
# omits those objects with CMake 3.28, leaving Lua profiles unavailable at runtime.
add_library(EightBitEditorObjects OBJECT QRegexSearch.h QRegexSearch.cpp)
set_target_properties(EightBitEditorObjects PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_link_libraries(EightBitEditorObjects PRIVATE Qt6::Core scintilla)
target_link_libraries(EightBitEditor INTERFACE "$<TARGET_OBJECTS:EightBitEditorObjects>")
target_sources(EightBitEditor INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts.qrc"
    "${CMAKE_CURRENT_SOURCE_DIR}/themes/themes.qrc")
add_dependencies(EightBitEditor EightBitEditorObjects)
