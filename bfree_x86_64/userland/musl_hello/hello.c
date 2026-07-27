/* Minimal musl-static hello for B2.20 gate. */
#include <unistd.h>

int main(void)
{
    const char msg[] = "B2_MUSL_HELLO_OK\n";
    (void)write(1, msg, sizeof(msg) - 1);
    return 0;
}
