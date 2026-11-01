-- SPDX-License-Identifier: GPL-3.0-or-later
-- Lexilla's C-family lexer supplies baseline Groovy highlighting. This does not
-- implement a Groovy parser, slashy-string grammar or semantic validation.
local L = require("language-profile")("java")
L.extensions = {"groovy", "gvy", "gy", "gsh", "gradle"}
L.tabSettings = "spaces"
L.tabSize = 4
L.keywords = {
    [0] = "as assert break case catch continue def default do else finally for if in instanceof new return switch throw throws try while true false null this super import",
    [1] = "abstract boolean byte char class const double enum extends final float implements int interface long native package private protected public short static strictfp synchronized trait transient void volatile var",
    [5] = "FIXME TODO XXX HACK NOTE",
}
L.properties["lexer.cpp.triplequoted.strings"] = "1"
L.styles["TRIPLE QUOTED STRING"] = {
    id = 21,
    fgColor = L.styles["STRING"].fgColor,
    bgColor = L.styles["STRING"].bgColor,
}
L.completionKeywords = "println print printf String Object List Map Set Closure BigDecimal BigInteger Integer Boolean Double Float Long Short Byte Character collect each eachWithIndex find findAll inject join size with withCloseable"
return L
