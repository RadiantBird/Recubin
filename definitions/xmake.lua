---@meta
-- Declarations for xmake's sandbox and project-description APIs.
-- This file is for Lua Language Server and is never executed by xmake.

---@param ... any
function set_project(...) end
function set_version(...) end
function set_languages(...) end
function set_runtimes(...) end
function set_default(...) end
function set_showmenu(...) end
function set_kind(...) end
function set_policy(...) end
function set_rundir(...) end
function add_rules(...) end
function add_cxflags(...) end
function add_cxxflags(...) end
function add_files(...) end
function add_includedirs(...) end
function add_defines(...) end
function add_linkdirs(...) end
function add_links(...) end
function add_syslinks(...) end
function add_frameworks(...) end
function add_deps(...) end
function option(...) end
function option_end() end
function target(...) end
function target_end() end
function rule(...) end
function rule_end() end
function before_build(...) end
function after_build(...) end
function on_load(...) end
---@param ... string
---@return boolean
function is_plat(...) end
---@param ... string
---@return boolean
function is_arch(...) end
---@param ... string
---@return boolean
function is_mode(...) end
---@param ... string
---@return boolean
function is_host(...) end
---@param name string
---@return any
function get_config(name) end
---@param name string
---@return any
function import(name) end
---@param message string
---@param ... any
function raise(message, ...) end

---@class xmake.path
---@field absolute fun(p: string): string
---@field join fun(...: string): string
---@field directory fun(p: string): string
---@field relative fun(p: string, root: string): string
---@field normalize fun(p: string): string
---@field basename fun(p: string): string
---@field extension fun(p: string): string
---@field split fun(p: string): string[]
path = {}

---@class xmake.hash
---@field sha256 fun(value: string): string
---@field strhash128 fun(value: string): string
hash = {}

---@param pattern string
---@return string[]
function os.files(pattern) end
---@param pattern string
---@return string[]
function os.dirs(pattern) end
---@return boolean
function os.isfile(...) end
---@return boolean
function os.isdir(...) end
---@return boolean
function os.exists(...) end
function os.mkdir(...) end
function os.cp(...) end
function os.rm(...) end
function os.mv(...) end
function os.execv(...) end
---@return string, string
function os.iorunv(...) end
function os.vrunv(...) end
function os.host(...) end
function os.arch(...) end
---@return string
function os.projectdir(...) end
---@return string
function io.readfile(...) end
function io.writefile(...) end
function table.join(...) end
function table.join2(...) end
---@return string[]
function string.split(...) end
---@return string
function string.trim(...) end
---@return boolean
function string.startswith(...) end
