tool { name = 'limp',
   projects = {
      app {
         deploy_bin = true,
         icon = 'icon/bengine-warm.ico',
         limp = {
            file = 'include/limp_lua.hpp',
            inputs = { 'meta/limp.lua' }
         },
         src = {
            'src/*.cpp'
         },
         libs = {
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
}
