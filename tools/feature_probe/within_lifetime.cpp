#include <type_traits>
int main(){ union U{int a; float b;}; U u{.a=1}; return std::is_within_lifetime(&u.b)?1:0; }
