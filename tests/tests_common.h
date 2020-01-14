#ifndef TESTS_COMMON_H
#define TESTS_COMMON_H

const char *get_dev_name(void);

#define T_SKIP_MSG "[ SKIP     ] "

#define SKIP_TEST_R(_reason) \
	do { \
		printf(T_SKIP_MSG "%s\n", _reason); \
		return; \
	} while (0)

#endif
