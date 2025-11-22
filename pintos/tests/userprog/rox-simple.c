/* Ensure that the executable of a running process cannot be
   modified. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void)
{
	int handle;
	char buffer[16];

	CHECK((handle = open("rox-simple")) > 1, "open \"rox-simple\"");
	CHECK(read(handle, buffer, sizeof buffer) == (int)sizeof buffer, "read \"rox-simple\"");
	// 자기 자신(rox-simple)을 열어서 쓰기를 시도하고, 0이 반환되길 기대함
	CHECK(write(handle, buffer, sizeof buffer) == 0, "try to write \"rox-simple\"");
}
