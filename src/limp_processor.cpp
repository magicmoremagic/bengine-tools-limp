#include "limp_processor.hpp"
#include "limp_lua.hpp"
#include <be/core/logging.hpp>
#include <be/util/zlib.hpp>
#include <be/util/get_file_contents.hpp>
#include <be/util/put_file_contents.hpp>
#include <be/util/fnv.hpp>
#include <be/util/string_span.hpp>
#include <be/belua/lua_helpers.hpp>
#include <be/core/lua_modules.hpp>
#include <be/util/lua_modules.hpp>
#include <be/blt/lua_modules.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>
#include <regex>
#include <sstream>

namespace be {
namespace limp {
namespace {

using Match = std::match_results<typename gsl::string_span<>::iterator>;
using Submatch = std::sub_match<typename gsl::string_span<>::iterator>;

///////////////////////////////////////////////////////////////////////////////
S regex_escape(const S& re) {
   std::ostringstream oss;
   for (char c : re) {
      switch (c) {
         case '-':
         case '[':
         case ']':
         case '\\':
         case '{':
         case '}':
         case '(':
         case ')':
         case '*':
         case '+':
         case '?':
         case '.':
         case ',':
         case '/':
         case '^':
         case '$':
         case '|':
         case '#':
         case ' ':
            oss << '\\' << c;
            break;
         case '\t':
            oss << "\\t";
            break;
         case '\r':
            oss << "\\r";
            break;
         case '\n':
            oss << "\\n";
            break;
         default:
            oss << c;
            break;
      }
   }
   return oss.str();
}

///////////////////////////////////////////////////////////////////////////////
std::regex& get_regex(const S& re) {
   thread_local std::unordered_map<S, std::regex> map;
   auto it = map.find(re);
   if (it == map.end()) {
      std::tie(it, std::ignore) = map.emplace(re, std::regex(re));
   }
   return it->second;
}

///////////////////////////////////////////////////////////////////////////////
gsl::string_span<> subspan(const gsl::string_span<>& span, const Submatch& submatch) {
   return span.subspan(submatch.first - span.begin(), submatch.second - submatch.first);
}

///////////////////////////////////////////////////////////////////////////////
gsl::string_span<> subspan(const gsl::string_span<>& span, typename gsl::string_span<>::iterator first, typename gsl::string_span<>::iterator last) {
   return span.subspan(first - span.begin(), last - first);
}

///////////////////////////////////////////////////////////////////////////////
S inflate_limp_core() {
   Buf<const UC> data = make_buf(BE_LIMP_COMPILED_LUA_MODULE, BE_LIMP_COMPILED_LUA_MODULE_LENGTH);
   return util::inflate_string(data, BE_LIMP_COMPILED_LUA_MODULE_UNCOMPRESSED_LENGTH);
}

///////////////////////////////////////////////////////////////////////////////
const S& get_limp_core() {
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
   S result;

   lua_State* L = context.L();
   lua_pushcfunction(L, lua_get_results);
   belua::ecall(L, 0, 1);

   if (lua_type(L, -1) == LUA_TSTRING) {
      std::size_t len;
      const char* ptr = lua_tolstring(L, -1, &len);
      if (ptr) {
         result.assign(ptr, len);
      }
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
int lua_set_global(lua_State* L) {
   lua_pushvalue(L, LUA_REGISTRYINDEX);
   lua_rawgeti(L, -1, LUA_RIDX_GLOBALS);
   lua_pushvalue(L, 1); // copy key
   lua_pushvalue(L, 2); // copy value
   lua_rawset(L, -3); // insert slot
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
void set_global(belua::Context& context, const char* field, gsl::cstring_span<> value) {
   lua_State* L = context.L();
   lua_pushcfunction(L, lua_set_global);
   lua_pushstring(L, field);
   lua_pushlstring(L, value.data(), value.length());
   belua::ecall(L, 2, 0);
}

///////////////////////////////////////////////////////////////////////////////
void set_global(belua::Context& context, const char* field, lua_Integer value) {
   lua_State* L = context.L();
   lua_pushcfunction(L, lua_set_global);
   lua_pushstring(L, field);
   lua_pushinteger(L, value);
   belua::ecall(L, 2, 0);
}

} // be::limp::()

///////////////////////////////////////////////////////////////////////////////
LimpProcessor::LimpProcessor(const Path& path, const LanguageConfig& comment, const LanguageConfig& limp, const Path& depfile_path)
   : path_(path),
     hash_path_(path.string() + ".limphash"),
     depfile_path_(depfile_path),
     comment_(comment),
     limp_(limp),
     opener_(regex_escape(comment.opener + limp.opener)),
     closer_("(" + regex_escape(limp.closer) + ")|(" + regex_escape(comment.closer) + ")"),
     loaded_(false),
     processable_calculated_(false),
     processable_(false) {
   
}

///////////////////////////////////////////////////////////////////////////////
bool LimpProcessor::processable() {
   load_();
   if (!processable_calculated_) {
      std::regex& re = get_regex(opener_);
      if (std::regex_search(disk_content_, re)) {
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
   bool retval = false; // set to true if we find stuff that needs to be replaced
   I32 limp_comment_number = 1;
   belua::Context context = make_context_();
   
   std::regex& open_re = get_regex(opener_);
   std::regex& close_re = get_regex(closer_);
   std::regex& comment_close_re = get_regex(regex_escape(comment_.closer));
   std::regex& lf_re = get_regex("\\r\\n?|\\n");
   
   Match m;
   std::ostringstream oss;
   gsl::string_span<> remaining = disk_content_;
   while (std::regex_search(remaining.begin(), remaining.end(), m, open_re)) {
      auto prefix = subspan(remaining, m.prefix());
      std::size_t indent_size = 0;
      for (auto rit = prefix.rbegin(), rend = prefix.rend(); rit != rend; ++rit) {
         char c = *rit;
         if (c == '\n' || c == '\r') {
            break;
         }
         ++indent_size;
      }
      gsl::string_span<> indent = prefix.subspan(prefix.size() - indent_size, indent_size);

      oss << prefix;
      oss << subspan(remaining, m[0]);
      remaining = subspan(remaining, m.suffix());

      if (std::regex_search(remaining.begin(), remaining.end(), m, close_re)) {
         gsl::string_span<> lua = subspan(remaining, m.prefix());
         gsl::string_span<> old_gen;
         if (m[1].matched) {
            // found end of limp before end of comment
            gsl::string_span<> temp = subspan(remaining, m.suffix());
            if (std::regex_search(temp.begin(), temp.end(), m, comment_close_re)) {
               // found end of comment
               std::istringstream iss(S(m.prefix().first, m.prefix().second));
               std::size_t lines;
               iss >> lines;
               if (iss) {
                  // parsed # of generated lines
                  S temp2;
                  iss >> temp2;
                  if (temp2.empty()) {
                     // no other non-ws before end of comment
                     old_gen = subspan(temp, m.suffix());
                     temp = old_gen;
                     while (lines > 0) {
                        if (std::regex_search(temp.begin(), temp.end(), m, lf_re)) {
                           temp = subspan(temp, m.suffix());
                           --lines;
                        } else {
                           break;
                        }
                     }
                     remaining = temp;
                     // remove remaining from end of old_gen
                     old_gen = old_gen.subspan(0, old_gen.size() - remaining.size());
                  } else {
                     be_notice() << "Stopping processing early!"
                        & attr(ids::log_attr_message) << "Unexpected data found after generated line count!"
                        & attr(ids::log_attr_input_path) << path_.generic_string()
                        | default_log();
                     break;
                  }
               } else {
                  be_notice() << "Stopping processing early!"
                     & attr(ids::log_attr_message) << "Could not parse generated line count!"
                     & attr(ids::log_attr_input_path) << path_.generic_string()
                     | default_log();
                  break;
               }
            } else {
               // reached end of file: print warning, don't process this comment
               be_notice() << "Stopping processing early!"
                  & attr(ids::log_attr_message) << "Unclosed LIMP comment!"
                  & attr(ids::log_attr_input_path) << path_.generic_string()
                  | default_log();
               break;
            }
         } else {
            // found end of comment; no existing generated code
            remaining = subspan(remaining, m.suffix());
         }

         prepare_(context, old_gen, indent);
         context.execute(lua, "@" + path_.filename().string() + " LIMP " + std::to_string(limp_comment_number));
         ++limp_comment_number;

         S new_gen = get_results(context);
         new_gen.append(1, '\n');
         //new_gen.append(indent.begin(), indent.end());

         new_gen = std::regex_replace(new_gen, lf_re, preferred_line_ending());

         std::size_t n_lines = std::count(new_gen.begin(), new_gen.end(), '\n');

         if (old_gen != gsl::string_span<>(new_gen)) {
            retval = true;
         }

         oss << lua << limp_.closer << ' ' << n_lines << ' ' << comment_.closer << new_gen;
      } else {
         // reached end of file: print warning, don't process this comment
         be_notice() << "Stopping processing early!"
            & attr(ids::log_attr_message) << "Unclosed LIMP comment!"
            & attr(ids::log_attr_input_path) << path_.generic_string()
            | default_log();
         break;
      }
   }
   oss << remaining;
   
   processed_content_ = oss.str();

   if (!depfile_path_.empty()) {
      S write_depfile = "if write_depfile then write_depfile() end";
      context.execute(write_depfile, "@" + path_.filename().string() + " write depfile");
   }

   return retval;
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::write() {
   util::put_file_contents(path_, processed_content_);
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
      util::put_file_contents(hash_path_, processed_content_hash);
      return true;
   }
   return false;
}

///////////////////////////////////////////////////////////////////////////////
void LimpProcessor::load_() {
   if (!loaded_) {
      disk_content_ = util::get_file_contents_string(path_);
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
void LimpProcessor::prepare_(belua::Context& context, gsl::cstring_span<> old_gen, gsl::cstring_span<> indent) {
   set_global(context, "last_generated_data", old_gen);
   set_global(context, "base_indent", indent);
}

} // be::limp
} // be
