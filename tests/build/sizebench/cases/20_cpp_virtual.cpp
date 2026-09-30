struct Base { virtual int f() const; };
struct Derived : Base { int f() const override; };
int Base::f() const { return 1; }
int Derived::f() const { return 2; }
Derived d;
Base *p = &d;
volatile int sink;
int main() { sink = p->f(); for (;;); }
