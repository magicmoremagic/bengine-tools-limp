#pragma once
#ifndef BE_LIMP_VERSION_HPP_
#define BE_LIMP_VERSION_HPP_

#include <be/core/macros.hpp>

#define BE_LIMP_VERSION_MAJOR 0
#define BE_LIMP_VERSION_MINOR 1
#define BE_LIMP_VERSION_REV 5

/*!! include('common/version', 'BE_LIMP', 'LIMP') !! 6 */
/* ################# !! GENERATED CODE -- DO NOT MODIFY !! ################# */
#define BE_LIMP_VERSION (BE_LIMP_VERSION_MAJOR * 100000 + BE_LIMP_VERSION_MINOR * 1000 + BE_LIMP_VERSION_REV)
#define BE_LIMP_VERSION_STRING "LIMP " BE_STRINGIFY(BE_LIMP_VERSION_MAJOR) "." BE_STRINGIFY(BE_LIMP_VERSION_MINOR) "." BE_STRINGIFY(BE_LIMP_VERSION_REV)

/* ######################### END OF GENERATED CODE ######################### */

#endif
