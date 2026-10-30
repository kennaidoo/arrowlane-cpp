#include <mdspan>
int main(){ int a[4]{}; std::mdspan<int,std::dextents<int,1>> m(a,4); auto s = std::submdspan(m, std::pair{0,2}); return s.extent(0)-2; }
