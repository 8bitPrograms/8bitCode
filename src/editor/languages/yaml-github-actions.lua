-- SPDX-License-Identifier: GPL-3.0-or-later
local L = require("language-profile")("yaml")
-- Path/content detection belongs to DevOpsProfiles. Sharing generic extensions
-- here would make pairs(languages) choose an arbitrary YAML dialect.
L.extensions = nil
L.tabSettings = "spaces"
L.tabSize = 2
L.completionKeywords = [[
name run-name on push pull_request workflow_dispatch workflow_call schedule cron
permissions contents actions checks deployments discussions id-token issues packages
pages pull-requests security-events statuses read write none env defaults run shell
working-directory concurrency group cancel-in-progress jobs needs if runs-on
environment outputs steps id uses with secrets inherit strategy matrix include
exclude fail-fast max-parallel timeout-minutes continue-on-error container services
ports volumes options credentials username password branches branches-ignore tags
tags-ignore paths paths-ignore types inputs description required type default
github runner job steps matrix needs inputs vars secrets always cancelled failure
success contains startsWith endsWith format join toJSON fromJSON hashFiles
]]
return L
