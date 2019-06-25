#include <linux/tee_drv.h> // tee
#include "../tee_private.h" /* struct tee_shm */
#include "../optee/optee_private.h" /* optee_rpc_param */

extern struct tee_device *ofs_tee;
extern struct tee_shm *tee_shm_alloc(struct tee_context *ctx, size_t size, u32 flags);
extern int tee_shm_get_pa(struct tee_shm *shm, size_t off, phys_addr_t *pa);
extern int	ofs_mkdir(const char *, int);
extern struct tee_shm *ofs_shm; /* Global message passing shared memory */
extern struct arm_smccc_res ofs_res;
extern struct tee_context *ofs_tee_context;
extern struct cma cma_areas[MAX_CMA_AREAS];



