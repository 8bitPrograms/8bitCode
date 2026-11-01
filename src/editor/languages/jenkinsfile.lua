-- SPDX-License-Identifier: GPL-3.0-or-later
local L = require("language-profile")("groovy")
L.extensions = nil
local pipelineWords = [[
pipeline agent any none label node docker dockerfile kubernetes stages stage steps
post always changed fixed regression aborted failure success unstable unsuccessful
cleanup environment credentials options parameters triggers tools input when
expression branch tag buildingTag changelog changeset changeRequest triggeredBy
beforeAgent beforeInput beforeOptions allOf anyOf not parallel matrix axes axis
excludes exclude values notValues failFast script timeout retry timestamps
disableConcurrentBuilds skipDefaultCheckout skipStagesAfterUnstable buildDiscarder
logRotator ansiColor cron pollSCM upstream choice booleanParam string text password
echo sh bat powershell pwsh checkout scm git dir ws withEnv withCredentials
archiveArtifacts stash unstash junit catchError error warnError unstable sleep waitUntil
readFile writeFile fileExists deleteDir cleanWs build input mail emailext tool
]]
L.completionKeywords = L.completionKeywords .. " " .. pipelineWords
-- Pipeline DSL identifiers get the existing semantic TYPE WORD theme color.
L.keywords[1] = L.keywords[1] .. " " .. pipelineWords
return L
