#include <memory>
#include <cstddef>
int main(){ alignas(int) std::byte b[sizeof(int)]{}; int* p = std::start_lifetime_as<int>(b); return p?0:1; }
