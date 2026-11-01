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

target_include_directories(EightBitEditor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
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

# Some objects must reach every consumer even though nothing references them
# from the consumer's own code. Unreferenced objects in a static library are
# dropped at link time, which would lose the static initialisers that register
# the Lua profiles and themes as Qt resources. Scintilla calls back into
# CreateRegexSearch(), so the regex hook must not sit in an archive that the
# linker scans before libscintilla. An object library linked through the
# interface goes straight into each consumer on every platform, without
# Q_INIT_RESOURCE calls or link-order tricks.
#
# The flip side: these objects are copied into every target that links
# EightBit::Editor. Link it only from final binaries (executables, tests),
# PRIVATE and once per binary, never through another static library, or the
# resource initialisers and CreateRegexSearch() would be defined twice.
add_library(EightBitEditorObjects OBJECT
    QRegexSearch.h QRegexSearch.cpp
    scripts.qrc
    themes/themes.qrc
)
set_target_properties(EightBitEditorObjects PROPERTIES AUTORCC ON AUTOMOC OFF AUTOUIC OFF)
target_link_libraries(EightBitEditorObjects PRIVATE Qt6::Core scintilla)
target_link_libraries(EightBitEditor INTERFACE "$<TARGET_OBJECTS:EightBitEditorObjects>")
add_dependencies(EightBitEditor EightBitEditorObjects)
