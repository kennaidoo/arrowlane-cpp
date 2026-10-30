#include <generator>
std::generator<int> g(){ co_yield 1; }
int main(){ for(int v: g()){ return v-1; } return 1; }
