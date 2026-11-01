-- SPDX-License-Identifier: GPL-3.0-or-later
local L = require("language-profile")("yaml")
L.extensions = nil
L.tabSettings = "spaces"
L.tabSize = 2
L.completionKeywords = [[
name hosts gather_facts become become_user become_method remote_user connection
vars vars_files vars_prompt roles tasks pre_tasks post_tasks handlers collections
module_defaults environment serial strategy order max_fail_percentage any_errors_fatal
block rescue always when failed_when changed_when register notify listen tags
loop loop_control loop_var label index_var until retries delay delegate_to delegate_facts
run_once ignore_errors ignore_unreachable no_log check_mode diff async poll throttle
args action local_action import_playbook import_tasks include_tasks include_role import_role
ansible.builtin.assert ansible.builtin.command ansible.builtin.copy ansible.builtin.debug
ansible.builtin.file ansible.builtin.get_url ansible.builtin.include_tasks
ansible.builtin.lineinfile ansible.builtin.package ansible.builtin.ping
ansible.builtin.service ansible.builtin.set_fact ansible.builtin.shell
ansible.builtin.stat ansible.builtin.template ansible.builtin.user ansible.builtin.wait_for
state present absent started stopped restarted enabled src dest owner group mode
content msg that fail_msg success_msg ansible_facts inventory_hostname hostvars groups
]]
return L
