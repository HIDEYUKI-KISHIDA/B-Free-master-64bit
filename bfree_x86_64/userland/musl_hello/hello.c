#include <stdio.h>
#include <unistd.h>

int main(void)
{
    puts("MUSL_HELLO_OK");
    fflush(stdout);
    /* _exit avoids musl atexit against a shared guest fd table after exec. */
    _exit(0);
    return 0;
}
