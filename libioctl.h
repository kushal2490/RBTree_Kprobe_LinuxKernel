#include <linux/ioctl.h>

#define IOC_MAGIC 'k'
#define SETEND _IOW(IOC_MAGIC, 0, unsigned int)
#define SETDUMP _IOW(IOC_MAGIC, 1, unsigned int)
#define PRINT _IOW(IOC_MAGIC, 2, unsigned int)
