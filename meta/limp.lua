-- Predefined global variables:
-- file_path
-- file_contents
-- file_hash
-- hash_file_path
-- comment_begin
-- comment_end

-- Globals set just before processing each LIMP comment:
-- last_generated_data
-- base_indent

local table = table
local debug = debug
local string = string
local tostring = tostring
local type = type
local select = select
local ipairs = ipairs
local dofile = dofile
local load = load
local getmetatable = getmetatable
local setmetatable = setmetatable

local fs = require('be.fs')
local util = require('be.util')
local blt = require('be.blt')

do -- strict.lua
   -- checks uses of undeclared global variables
   -- All global variables must be 'declared' through a regular assignment
   -- (even assigning nil will do) in a main chunk before being used
   -- anywhere or assigned to inside a function.
   local mt = getmetatable(_G)
   if mt == nil then
      mt = {}
      setmetatable(_G, mt)
   end

   __STRICT = true
   mt.__declared = {}

   mt.__newindex = function (t, n, v)
      if __STRICT and not mt.__declared[n] then
         local w = debug.getinfo(2, "S").what
         if w ~= "main" and w ~= "C" then
            error("assign to undeclared variable '"..n.."'", 2)
         end
         mt.__declared[n] = true
      end
      rawset(t, n, v)
   end
  
   mt.__index = function (t, n)
      if __STRICT and not mt.__declared[n] and debug.getinfo(2, "S").what ~= "C" then
         error("variable '"..n.."' is not declared", 2)
      end
      return rawget(t, n)
   end

   function global(...)
      for _, v in ipairs{...} do mt.__declared[v] = true end
   end

end

indent_size = 3
indent_char = ' '
limprc_path = nil
prefix = nil
postfix = nil
base_indent = nil
root_dir = nil

function trim_trailing_ws (str)
   return str:gsub('[ \t]+(\r?\n)', '%1'):gsub('[ \t]+$', '')
end

function postprocess (str)
   return trim_trailing_ws(str)
end

do -- indent
   local current_indent = 0

   function get_indent ()
      local retval = ''
      if base_indent ~= nil and base_indent ~= '' then
         retval = base_indent
      end
      return retval .. string.rep(indent_char, current_indent * indent_size)
   end 

   function write_indent ()
      if base_indent ~= nil and base_indent ~= '' then
         write(base_indent)
      end
      local indent = string.rep(indent_char, current_indent * indent_size)
      if indent ~= '' then
         write(indent)
      end
   end

   function reset_indent ()
      current_indent = 0
   end

   function indent (count)
      if count == nil then count = 1 end
      current_indent = current_indent + count
   end

   function unindent (count)
      if count == nil then count = 1 end
      current_indent = current_indent - count
   end

   function set_indent (count)
      current_indent = count
   end

end

function indent_newlines (str)
   str = str:gsub('\n', '\n' .. get_indent())
   return str
end

do -- write
   local out = nil
   local n = 1

   local function init ()
      reset_indent()
      out = { }
      n = 1

      write_prefix()
   end

   function nl ()
      if out == nil then
         init()
      end
      out[n] = '\n'
      n = n + 1
      write_indent()
   end

   function write (...)
      if out == nil then
         init()
      end
      for i = 1, select('#', ...) do
         local x = select(i, ...)
         if x ~= nil then
            out[n] = x
            n = n + 1
         end
      end
   end

   function writeln (...)
      if out == nil then
         init()
      end
      for i = 1, select('#', ...) do
         local x = select(i, ...)
         if x ~= nil then
            out[n] = x
            n = n + 1
         end
      end
      nl()
   end

   function write_lines (...)
      if out == nil then
         init()
      end
      for i = 1, select('#', ...) do
         local x = select(i, ...)
         if x ~= nil then
            out[n] = x
            n = n + 1
         end
         nl()
      end
   end

   function reset ()
      if out == nil then
         init()
      end
      
      write_postfix()

      local str = table.concat(out)
      out = nil

      if type(postprocess) == 'function' then
         str = postprocess(str)
      end

      return str
   end
end

function write_prefix ()
   if prefix ~= nil then
      write(prefix)
   else
      nl()
      writeln(comment_begin, ' ################# !! GENERATED CODE -- DO NOT MODIFY !! ################# ', comment_end)
   end
end

function write_postfix ()
   reset_indent()
   if postfix ~= nil then
      write(postfix)
   else
      nl()
      write(comment_begin,   ' ######################### END OF GENERATED CODE ######################### ', comment_end)
   end
end

do -- dependencies
   local deps = { }

   function get_depfile_target ()
      return be.fs.ancestor_relative(file_path, root_dir)
   end

   function write_depfile ()
      if not depfile_path or depfile_path == '' then
         return
      end

      local depfile = ''
      local depfile_exists = false
      if fs.exists(depfile_path) then
         depfile = fs.get_file_contents(depfile_path)
         depfile_exists = true
      end

      do
         local prefix = get_depfile_target() .. ':'
         local depfile_line = { prefix }
         for k, v in pairs(deps) do
            depfile_line[#depfile_line + 1] = ' '
            depfile_line[#depfile_line + 1] = k
         end
         depfile_line = table.concat(depfile_line)

         local found_existing
         depfile = depfile:gsub(blt.gsub_escape(prefix) .. '[^\r\n]+', function ()
            found_existing = true
            return depfile_line
         end, 1)

         if not found_existing then
            depfile = depfile .. depfile_line .. '\n'
         end
      end

      if not depfile_exists then
         fs.create_dirs(fs.parent_path(depfile_path))
      end
      fs.put_file_contents(depfile_path, depfile)
   end

   function dependency (path)
      if path and path ~= '' then
         deps[path] = true
      end
   end
end

require_load = util.require_load
function require_load_file (path, chunk_name)
   if not fs.exists(path) then
      error('Path \'' .. path .. '\' does not exist!')
   end
   if not chunk_name then
      chunk_name = '@' .. fs.path_filename(path)
   end
   dependency(fs.ancestor_relative(path, root_dir))
   local contents = fs.get_file_contents(path)
   return util.require_load(contents, chunk_name)
end

get_template = blt.get_template
register_template_dir = blt.register_template_dir
register_template_file = blt.register_template_file
register_template_string = blt.register_template_string

pgsub = blt.pgsub
explode = blt.explode
pad = blt.pad
rpad = blt.rpad
lpad = blt.lpad

function template (template_name, ...)
   return blt.get_template(template_name)(...)
end

function write_template (template_name, ...)
   write(indent_newlines(template(template_name, ...)))
end

function write_file (path)
   if fs.exists(path) then
      dependency(fs.ancestor_relative(path, root_dir))
      write(indent_newlines(fs.get_file_contents(path)))
   end
end

do -- include
   local chunks = { }
   local include_dirs = { }

   function get_include (include_name)
      if not include_name then
         error 'Must specify include script name!'
      end
      
      local existing = chunks[include_name]
      if existing ~= nil then
         return existing
      end

      local path = fs.find_file(include_name, table.unpack(include_dirs))
      if path then
         dependency(fs.ancestor_relative(path, root_dir))
         local contents = fs.get_file_contents(path)
         local fn = util.require_load(contents, '@' .. include_name)
         chunks[include_name] = fn
         return fn
      end

      path = fs.find_file(include_name .. '.lua', table.unpack(include_dirs))
      if path then
         dependency(fs.ancestor_relative(path, root_dir))
         local contents = fs.get_file_contents(path)
         local fn = util.require_load(contents, '@' .. include_name .. '.lua')
         chunks[include_name] = fn
         return fn
      end

      error('No include found matching \'' .. include_name .. '\'')
   end

   function register_include_dir (path)
      local n = #include_dirs
      for i = 1, n do
         if include_dirs[i] == path then
            return
         end
      end
      include_dirs[n + 1] = fs.canonical(path)
   end

   function resolve_include_path (path)
      return fs.resolve_path(path, include_dirs) or fs.resolve_path(path .. '.lua', include_dirs)
   end
end


function include (include_name, ...)
   return get_include(include_name)(...)
end

function import_limprc (path)
   local p = fs.compose_path(path, '.limprc')
   if fs.exists(p) then
      limprc_path = p
      root_dir = path
      dofile(p)
      return true
   end

   if fs.root_path(path) == path then
      root_dir = path
      return false
   end

   return import_limprc(fs.parent_path(path))
end

import_limprc(fs.parent_path(file_path))
