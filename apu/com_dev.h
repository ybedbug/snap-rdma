#ifndef __COM_DEV_H__
#define __COM_DEV_H__

#include <libflexio-dev/flexio_dev.h>
#include "com_context_dev.h"

#define STUCK() do { \
		flexio_dev_return();                              \
		TRACE("You should never reach line: ", __LINE__); \
} while (1)

void swap_macs(char *packet);

#endif
