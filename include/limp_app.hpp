#pragma once
#ifndef BE_LIMP_LIMP_APP_HPP_
#define BE_LIMP_LIMP_APP_HPP_

#include "language_config.hpp"
#include <be/core/lifecycle.hpp>
#include <be/core/filesystem.hpp>
#include <unordered_map>
#include <set>
#include <vector>

#define BE_LIMP_VERSION_MAJOR 0
#define BE_LIMP_VERSION_MINOR 1
#define BE_LIMP_VERSION_REV 1
#define BE_LIMP_VERSION "LIMP " BE_STRINGIFY(BE_LIMP_VERSION_MAJOR) "." BE_STRINGIFY(BE_LIMP_VERSION_MINOR) "." BE_STRINGIFY(BE_LIMP_VERSION_REV)

namespace be {
namespace limp {

///////////////////////////////////////////////////////////////////////////////
class LimpApp final {
public:
   LimpApp(int argc, char** argv);
   int operator()();

private:
   void init_default_langs_();
   void load_langs_();
   void get_paths_(const S& pathspec);
   void process_(const Path& path);

   CoreInitLifecycle init_;
   CoreLifecycle core_;
   std::unordered_map<S, LanguageConfig> langs_;
   I8 status_ = 0;
   bool recursive_ = false;
   bool dry_run_ = false;
   bool stop_on_failure_ = false;
   bool force_process_ = false;
   Path depfile_path_;
   std::vector<Path> search_paths_;
   std::vector<S> jobs_;
   std::set<Path> paths_;
};

} // be::limp
} // be

#endif
