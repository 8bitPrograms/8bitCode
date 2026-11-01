-- SPDX-License-Identifier: GPL-3.0-or-later
-- Clone a definition without mutating Lua's cached require() result. Styles
-- retain their semantic names so the application's theme overlays still apply.
local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, item in pairs(value) do result[key] = copy(item) end
    return result
end
return function(baseName) return copy(require(baseName)) end
