volatile int v = 42;
volatile int sink;
int *volatile keep;   /* escape the pointer so allocation elision cannot remove new/delete */
int main() {
    int *p = new int(v);
    keep = p;
    sink = *keep;
    delete keep;
    for (;;);
}
