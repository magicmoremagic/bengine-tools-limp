#pragma once
#ifndef BE_LIMP_LIMP_APP_HPP_
#define BE_LIMP_LIMP_APP_HPP_

#include "language_config.hpp"
#include <be/core/lifecycle.hpp>
#include <be/core/filesystem.hpp>
#include <unordered_map>
#include <set>
#include <vector>

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
   bool test_ = false;
   bool recursive_ = false;
   bool dry_run_ = false;
   bool stop_on_failure_ = false;
   bool force_process_ = false;
   bool write_hashes_ = false;
   Path depfile_path_;
   std::vector<Path> search_paths_;
   std::vector<S> jobs_;
   std::set<Path> paths_;
};

} // be::limp
} // be

#endif
