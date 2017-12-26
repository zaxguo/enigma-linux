/* lwg: This kernel module serves two goals:
 * 1: kick starts the TA in secure world
 * 2: handle the request issued by secure world */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/slab.h> // mem
#include <linux/tee_drv.h> // tee
#include "../tee_private.h" /* struct tee_shm */
#include "../optee/optee_private.h" //optee
#include "../optee/optee_smc.h" //optee
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <asm/io.h>  // virt_to_phys
#include <linux/syscalls.h> // syscall
#include <linux/fs.h>
#include <linux/unistd.h>

#include "ofs_msg.h" /* TODO: may move this to kernel header for later use */
#include "ofs_private.h"  /* some utility functions */
#include "ofs_ops.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("LWG");
MODULE_DESCRIPTION("OFS Normal World Handler Interacting with TEE");

extern struct tee_device *ofs_tee;
extern struct tee_shm *tee_shm_alloc(struct tee_context *ctx, size_t size, u32 flags);
extern int tee_shm_get_pa(struct tee_shm *shm, size_t off, phys_addr_t *pa);

extern int	ofs_mkdir(const char *, int);

static int ofs_tee_open(struct tee_device *tee) {
	struct tee_context *ofs_context;
	int rc = 0;
	ofs_context = kzalloc(sizeof(*ofs_context), GFP_KERNEL);
	ofs_context->teedev = tee;
	INIT_LIST_HEAD(&ofs_context->list_shm);
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param;
	arg.func = 1;
	arg.session = 0;
	arg.cancel_id = 2;
	arg.num_params = 1;


	param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param.u.value.a = 0;
	param.u.value.b = 1;
	param.u.value.c = 2;
	if (!tee->desc->ops->invoke_func) {
		printk(KERN_ERR"lwg:%s:cannot find invoke func!\n", __func__);
		return 1;
	}
	rc = tee->desc->ops->invoke_func(ofs_context, &arg , &param);
	if (rc) {
		printk("lwg:%s:error %d\n", __func__, rc);
	}
	return 0;
}


static int ofs_smc(void) {
	struct optee_rpc_param param = {};
	struct arm_smccc_res res;
	struct tee_context *ctx;
	struct tee_shm *shm;
	int rc;
	int i;
	phys_addr_t shm_pa;
	struct ofs_msg *msg;
	int is_first;
	int op;

	/* Init */
	is_first = 1;
	op = 0;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		printk(KERN_ERR"lwg:%s:NO MEM\n", __func__);
	}
	ctx->teedev = ofs_tee;
	INIT_LIST_HEAD(&ctx->list_shm);

	/* First allocate the shm */
	shm = tee_shm_alloc(ctx, 4096, TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(shm)) {
		return PTR_ERR(shm);
	}
	printk("lwg:%s:shm allocated va@ %p\n", __func__, shm);
	rc = tee_shm_get_pa(shm, 0, &shm_pa);
	printk("lwg:%s:shm allocated pa@ %16llx\n", __func__, shm_pa);
	/* Then, setup the args according to calling convention */

	param.a0 = OPTEE_SMC_CALL_WITH_ARG;
	/* Pair the paddr of allocated shared mem into register a1 and a2
	 * The shm is used for message passing  */
	reg_pair_from_64(&param.a1, &param.a2, shm_pa);
	/* lwg: These are not loaded into register due to the exeception
	 * hanler in sec world don't load them */
	param.a3 = 1;
	param.a4 = 2;
	param.a5 = 3;
	param.a6 = 4;
	param.a7 = 5;

	/* Do the SMC, invoke func is arm_smccc_smc */
	while(true) {
		arm_smccc_smc(param.a0, param.a1, param.a2, param.a3, param.a4, param.a5, param.a6, param.a7, &res);
		if (OPTEE_SMC_RETURN_IS_RPC(res.a0)) {

			printk("lwg:%s:catch an RPC, dump return value:\n", __func__);
			printk("lwg:a0 = %08x\n", res.a0);
			printk("lwg:a1 = %08x\n", res.a1);
			printk("lwg:a2 = %08x\n", res.a2);
			printk("lwg:a3 = %08x\n", res.a3);

			if (!is_first)
				break;
			if (is_first)
				is_first = 0;
			msg = recv_ofs_msg(shm);
			if (msg) {
				op = msg->msg.fs_request.request;
				char *dir = msg->msg.fs_request.filename;
				switch (op) {
					case OFS_MKDIR:
						printk("lwg:%s:mkdirat:%d:\"%s\"\n", __func__, msg->msg.fs_request.request, msg->msg.fs_request.filename);
						ofs_mkdir(dir, 0777);
						/* may change this as needed */
						param.a0 = OPTEE_SMC_CALL_RETURN_FROM_RPC;
						break;
					case OFS_OPEN:
						break;
					case OFS_READ:
						break;
					case OFS_WRITE:
						break;
					default:
						BUG();
				}
				param.a1 = res.a1;
				param.a2 = res.a2;
				param.a3 = res.a3; /* lwg: be careful not to touch a3 as it is used for thread id */
				printk("lwg:%s:returning to sec world\n", __func__);
			}
		} else {
			rc = res.a0;
			break;
		}
	}
	return rc;
}

static int __init ofs_init(void)
{
	int rc;
	if (!ofs_tee) {
		printk(KERN_ERR"lwg:%s:cannot find tee class!\n",__func__);
		return 1;
	}
	printk(KERN_INFO"lwg:%s:sucess, find tee device\n",__func__);
	printk(KERN_INFO"lwg:%s:PADDR of ofs_tee is %16llx, VADDR of ofs_tee is %p, ofs_tee is stored in %p\n", __func__, virt_to_phys(ofs_tee), ofs_tee, (void *)(&ofs_tee));
	rc = ofs_smc();  /* kickstart */
	return 0;
}

static void __exit ofs_cleanup(void)
{
	printk(KERN_INFO "Cleaning up module.\n");
	ofs_tee=NULL;
}

module_init(ofs_init);
module_exit(ofs_cleanup);
