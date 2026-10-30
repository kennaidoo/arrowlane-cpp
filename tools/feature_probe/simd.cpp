#include <experimental/simd>
int main(){ std::experimental::simd<int> v(1); return v[0]-1; }
