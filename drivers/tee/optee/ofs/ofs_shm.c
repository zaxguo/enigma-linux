#include <linux/tee_drv.h>
#include <linux/arm-smccc.h>

struct tee_shm *ofs_shm;
EXPORT_SYMBOL(ofs_shm);

struct arm_smccc_res ofs_res;
EXPORT_SYMBOL(ofs_res);
