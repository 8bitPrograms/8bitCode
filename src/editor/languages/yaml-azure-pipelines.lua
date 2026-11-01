-- SPDX-License-Identifier: GPL-3.0-or-later
local L = require("language-profile")("yaml")
L.extensions = nil
L.tabSettings = "spaces"
L.tabSize = 2
L.completionKeywords = [[
name trigger pr schedules cron displayName branches include exclude batch always
pool vmImage demands variables group template parameters type default values
resources repositories repository containers container pipelines pipeline builds
packages webhooks stages stage dependsOn condition jobs job deployment environment
strategy matrix parallel runOnce rolling canary steps task script bash pwsh
powershell checkout download downloadBuild getPackage publish reviewApp inputs
env workingDirectory failOnStderr continueOnError timeoutInMinutes retryCountOnTaskFailure
enabled target persistCredentials clean fetchDepth submodules path artifact patterns
extends lockBehavior appendCommitMessageToRunName workspace cancelTimeoutInMinutes
services uses succeeded failed canceled succeededOrFailed always and or not eq ne
contains containsValue startsWith endsWith format coalesce counter each if else elseif
]]
return L
