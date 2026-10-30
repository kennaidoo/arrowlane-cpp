#include <memory>
#include <cstddef>
#include <span>
int main(){ alignas(8) std::byte b[16]{}; int* p = std::start_lifetime_as_array<int>(b, 4); return p?0:1; }
