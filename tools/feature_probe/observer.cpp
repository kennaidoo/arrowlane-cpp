#include <experimental/memory>
int main(){ int x=0; std::experimental::observer_ptr<int> p(&x); return *p; }
