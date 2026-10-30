#include <memory>
#include <cstddef>
int main(){ alignas(8) std::byte b[16]{}; const std::byte* p=b; const int* q = std::start_lifetime_as_array<int>(p,4); return q?0:1; }
