#include "limp_app.hpp"

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv) {
   be::limp::LimpApp app(argc, argv);
   return app();
}
