#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/mman.h>

#include "tests_common.h"
#include "gtest/gtest.h"

const char *get_dev_name(void)
{
	const char *dev_name;

	dev_name = getenv("MLXDEV");
	if (!dev_name)
		dev_name = "mlx5_2";

	return dev_name;
}
