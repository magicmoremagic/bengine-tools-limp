tool 'limp' {
   app {
      icon 'icon/bengine-warm.ico',
      limp { file = 'include/limp_lua.hpp', inputs = 'meta/limp.lua' },
      link_project {
         'core',
         'core-id',
         'util',
         'ctable',
         'cli',
         'blt',
         'luacore',
         'luautil',
         'luablt',
         'zlib-static'
      }
   }
}
