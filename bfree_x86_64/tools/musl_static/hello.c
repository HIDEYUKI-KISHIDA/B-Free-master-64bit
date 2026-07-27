/* musl-gcc hello when toolchain is available */
#include <unistd.h>

int main(void)
{
	const char msg[] = "MUSL_STATIC\n";

	write(1, msg, sizeof(msg) - 1);
	return 0;
}
