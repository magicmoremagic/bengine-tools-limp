tool 'limp' {
   app {
      icon 'icon/bengine-warm.ico',
      limp { file = 'include/limp_lua.hpp', inputs = 'meta/limp.lua' },
      link_project {
         'core-id-with-names',
         'cli',
         'luautil',
         'luablt'
      }
   }
}
