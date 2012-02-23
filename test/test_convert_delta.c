#include "test.h"
#include "convert.h"

TEST(test_convert_delta)
{
	uint8 buffer0[10] = { 0, 1, 2, 3,  4,  5,  6, -7,  8, -29 };
	uint8 buffer1[10] = { 0, 1, 2, 3,  4,  5,  6, -7,  8, -29 };
	uint8 conv_r0[10] = { 0, 1, 3, 6, 10, 15, 21, 14, 22,  -7 };
	uint8 conv_r1[10] = { 0, 1, 2, 4,  6,  9, 12,  2, 20, 229 };


	convert_delta(10, 0, buffer0);
	fail_unless(memcmp(buffer0, conv_r0, 10) == 0,
				"Invalid 8-bit conversion");

	convert_delta(5, 1, buffer1);
	fail_unless(memcmp(buffer1, conv_r1, 10) == 0,
				"Invalid 16-bit conversion");
}
END_TEST
