#include "limp_processor.hpp"
#include "limp_lua.hpp"
#include <be/core/logging.hpp>
#include <be/util/zlib.hpp>
#include <be/util/get_file_contents.hpp>
#include <be/util/put_file_contents.hpp>
#include <be/util/fnv.hpp>
#include <be/util/line_endings.hpp>
#include <be/belua/lua_helpers.hpp>
#include <be/core/lua_modules.hpp>
#include <be/util/lua_modules.hpp>
#include <be/blt/lua_modules.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>
#include <sstream>

namespace be::limp {
namespace {

///////////////////////////////////////////////////////////////////////////////
S inflate_limp_core() {
#ifdef BE_LIMP_COMPILED_LUA_MODULE_UNCOMPRESSED_LENGTH
   Buf<const UC> data = make_buf(BE_LIMP_COMPILED_LUA_MODULE, BE_LIMP_COMPILED_LUA_MODULE_LENGTH);
   return util::inflate_string(data, BE_LIMP_COMPILED_LUA_MODULE_UNCOMPRESSED_LENGTH);
#else
   return S(BE_LIMP_COMPILED_LUA_MODULE, BE_LIMP_COMPILED_LUA_MODULE_LENGTH);
#endif
}

///////////////////////////////////////////////////////////////////////////////
SV get_limp_core() {
   static S limp_core = inflate_limp_core();
   return limp_core;
}

///////////////////////////////////////////////////////////////////////////////
int lua_get_results(lua_State* L) {
   lua_getglobal(L, "reset");
   lua_call(L, 0, 1);
   return 1;
}

///////////////////////////////////////////////////////////////////////////////
S get_results(belua::Context& context) {
   SV result;

   lua_State* L = context.L();
   lua_pushcfunction(L, lua_get_results);
   belua::ecall(L, 0, 1);
   SV raw = belua::get_string_view(L, -1, SV());
   return util::normalize_newlines_copy(raw);
}

///////////////////////////////////////////////////////////////////////////////
void set_global(belua::Context& context, const char* field, SV value) {
   lua_State* L = context.L();
   belua::push_string(L, value);
   lua_setglobal(L, field);
}

///////////////////////////////////////////////////////////////////////////////
void set_global(belua::Context& context, const char* field, lua_Integer value) {
   lua_State* L = context.L();
   lua_pushinteger(L, value);
   lua_setglobal(L, field);
}

} // be::limp::()

///////////////////////////////////////////////////////////////////////////////
LimpProcessor::LimpProcessor(const Path& path, const LanguageConfig& comment, const LanguageConfig& limp, const Path& depfile_path)
   : path_(path),
     hash_path_(path.string() + ".limphash"),
     depfile_path_(depfile_path),
     comment_(comment),
     limp_(limp),
     loaded_(false),
     processable_calculated_(false),
     processable_(false) { }

///////////////////////////////////////////////////////////////////////////////
bool LimpProcessor::processable() {
   load_();
   if (!processable_calculated_) {
      S search_str = comment_.opener + limp_.opener;
      if (S::npos != disk_content_.find(search_str)) {
         processable_ = true;
      }
      processable_calculated_ = true;
   }
   return processable_;
}

///////////////////////////////////////////////////////////////////////////////
bool LimpProcessor::should_process() {
   if (!processable()) {
      return false;
   }

   if (fs::exists(hash_path_)) {
      disk_hash_ = util::get_file_contents_string(hash_path_);
      boost::trim(disk_hash_);
      disk_content_hash_ = util::fnv256_1a(disk_content_);
      return disk_hash_ != disk_content_hash_;
   } else {
      return true;
   }
}

///////////////////////////////////////////////////////////////////////////////
bool LimpProcessor::process() {
   using namespace std::literals::string_view_literals;

   bool modified_file = false; // set to true if we find stuff that needs to be replaced

   I32 limp_comment_number = 1;
   belua::Context context = make_context_();

   SV remaining = disk_content_;
   S opener = comment_.opener + limp_.opener;
   std::ostringstream oss;

   const char limp_closer_initial_char = limp_.closer.front();
   const char comment_closer_initial_char = comment_.closer.front();

   for (;;) {
      auto opener_begin = remaining.find(opener);
      if (opener_begin == SV::npos) {
         break;
      }

      // Found a limp!
      SV prefix = remaining.substr(0, opener_begin);
      oss << prefix;
      oss << opener;
      remaining.remove_prefix(opener_begin + opener.size());

      // determine indent string for each generated line
      SV indent = prefix;
      std::size_t prev_nl = prefix.rfind('\n'); // not checking for \r because we should have opened the file in text mode
      if (prev_nl != SV::npos) {
         indent.remove_prefix(prev_nl + 1);
      }

      // find limp program and number of previously generated lines, followed by comment closer, and remove it from remaining
      std::size_t lines = 0;
      SV program = remaining;
      for (auto it = remaining.begin(), end = remaining.end(); it != end; ++it) {
         char c = *it;
         if (c == limp_closer_initial_char) {
            if (limp_.closer == remaining.substr(it - remaining.begin(), limp_.closer.size())) {
               // found limp closer, check line count
               program.remove_suffix(end - it);
               remaining.remove_prefix((it - remaining.begin()) + limp_.closer.size());

               SV linespec = remaining;

               auto comment_close_begin = remaining.find(comment_.closer);
               if (comment_close_begin == SV::npos) {
                  // no comment closer
                  remaining = SV();
               } else {
                  linespec.remove_suffix(linespec.size() - comment_close_begin);
                  remaining.remove_prefix(comment_close_begin + comment_.closer.size());
               }

               std::istringstream iss = std::istringstream(S(linespec));
               iss >> lines;
               break;
            }
         }
         if (c == comment_closer_initial_char) {
            if (comment_.closer == remaining.substr(it - remaining.begin(), comment_.closer.size())) {
               // found comment closer, no line count
               program.remove_suffix(end - it);
               remaining.remove_prefix((it - remaining.begin()) + comment_.closer.size());
               break;
            }
         }
      }

      // capture next `lines` lines from remaining into old_gen
      SV old_gen;
      if (lines > 0) {
         std::size_t old_gen_end = 0;
         while (lines > 0) {
            old_gen_end = remaining.find('\n', old_gen_end);
            if (old_gen_end == SV::npos) {
               break;
            }
            --lines;
            ++old_gen_end;
         }
         old_gen = remaining.substr(0, old_gen_end);
         remaining.remove_prefix(old_gen.size());
         if (!old_gen.empty() && old_gen.back() == '\n') {
            old_gen.remove_suffix(1);
         }
      }

      prepare_(context, old_gen, indent);

      S limp_name;
      const S limp_path = path_.filename().string();
      const S limp_number_str = std::to_string(limp_comment_number);
      limp_name.reserve(7 + limp_path.size() + limp_number_str.size());
      limp_name.append(1, '@');
      limp_name.append(limp_path);
      limp_name.append(" LIMP "sv);
      limp_name.append(limp_number_str);

      context.execute(program, limp_name);
      ++limp_comment_number;

      S new_gen = get_results(context);
      lines = 1 + std::count(new_gen.begin(), new_gen.end(), '\n');

      oss << program << limp_.closer << ' ' << lines << ' ' << comment_.closer << new_gen << '\n';
      
      if (old_gen != new_gen) {
         modified_file = true;
      }
   }

   oss << remaining;

   processed_content_ = oss.str();

   if (!depfile_path_.empty()) {
      SV write_depfile = "if write_depfile then write_depfile() end"sv;
      context.execute(write_depfile, "@" + path_.filename().string() + " write depfile");
   }

   return modified_file;
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::write() {
   util::put_text_file_contents(path_, processed_content_);
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::clear_hash() {
   if (fs::exists(hash_path_)) {
      fs::remove(hash_path_);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool LimpProcessor::write_hash() {
   S processed_content_hash = util::fnv256_1a(processed_content_);
   if (processed_content_hash != disk_hash_) {
      util::put_text_file_contents(hash_path_, processed_content_hash);
      return true;
   }
   return false;
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::load_() {
   if (!loaded_) {
      disk_content_ = util::get_text_file_contents_string(path_);
      loaded_ = true;
   }
}

///////////////////////////////////////////////////////////////////////////////
belua::Context LimpProcessor::make_context_() {
   belua::Context context({
      belua::id_module,
      belua::logging_module,
      belua::interpolate_string_module,
      belua::time_module,
      belua::util_module,
      belua::fs_module,
      belua::fnv256_module,
      belua::blt_module,
      belua::blt_compile_module,
      belua::blt_debug_module
   });

   set_global(context, "file_path", path_.string());
   set_global(context, "file_dir", path_.parent_path().string());
   set_global(context, "file_hash", disk_content_hash_);
   set_global(context, "hash_file_path", hash_path_.string());
   set_global(context, "depfile_path", depfile_path_.string());
   set_global(context, "file_contents", disk_content_);
   set_global(context, "comment_begin", comment_.opener);
   set_global(context, "comment_end", comment_.closer);

   context.execute(get_limp_core(), "@LIMP core");

   return context;
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::prepare_(belua::Context& context, SV old_gen, SV indent) {
   set_global(context, "last_generated_data", old_gen);
   set_global(context, "base_indent", indent);
}

} // be::limp
