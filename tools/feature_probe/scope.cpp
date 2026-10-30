#include <experimental/scope>
int main(){ auto g = std::experimental::scope_exit([]{ }); return 0; }
