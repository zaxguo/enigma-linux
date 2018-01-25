/* lwg: This kernel module serves two goals:
 * 1: kick starts the TA in secure world
 * 2: handle the request issued by secure world */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/slab.h> // mem
#include <linux/tee_drv.h> // tee
#include "../tee_private.h" /* struct tee_shm */
#include "../optee/optee_private.h" /* optee_rpc_param */
#include "../optee/optee_smc.h" //optee
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <asm/io.h>  // virt_to_phys
#include <linux/syscalls.h> // syscall
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/mm.h> /* ioremap */
#include <asm/page.h>

#include <ofs/ofs_msg.h> /* struct ofs_msg */
#include <ofs/ofs_util.h>  /* some utility functions */
#include <ofs/ofs_opcode.h>
#include "ofs_handler.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("LWG");
MODULE_DESCRIPTION("OFS Normal World Handler Interacting with TEE");

extern struct tee_device *ofs_tee;
extern struct tee_shm *tee_shm_alloc(struct tee_context *ctx, size_t size, u32 flags);
extern int tee_shm_get_pa(struct tee_shm *shm, size_t off, phys_addr_t *pa);

extern int	ofs_mkdir(const char *, int);

extern struct tee_shm *ofs_shm; /* Global message passing shared memory */
extern struct arm_smccc_res ofs_res;

static int ofs_tee_open(struct tee_device *tee) {
	struct tee_context *ofs_context;
	struct tee_ioctl_invoke_arg arg;
	struct tee_param param;
	int rc = 0;

	ofs_context = kzalloc(sizeof(*ofs_context), GFP_KERNEL);
	ofs_context->teedev = tee;
	INIT_LIST_HEAD(&ofs_context->list_shm);
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



static int ofs_handle_msg(struct ofs_msg *msg) {
	int opcode = msg->op;
	int rc;
	printk("lwg:%s:opcode = %d\n", __func__, opcode);
	switch (opcode) {
		case OFS_FS_REQUEST:
			rc = ofs_handle_fs_msg(msg);
			break;
		/* Note that these ``responses'' from sec world shouldn't
		 * be handled by normal world ... */
		case OFS_PG_REQUEST:
		case OFS_BLK_REQUEST:
			BUG();
	}
	return 0;
}


static int ofs_bench(void) {
//	struct arm_smccc_res *res;
	struct tee_context *ctx;
	int rc;
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

	/* allocate shm */
	ofs_shm = tee_shm_alloc(ctx, 4096, TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(ofs_shm)) {
		return PTR_ERR(ofs_shm);
	}
	printk("lwg:%s:shm allocated va@ %p\n", __func__, ofs_shm);
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	printk("lwg:%s:shm allocated pa@ %16llx\n", __func__, shm_pa);
	/* prepare the page request */
	ofs_pg_request(0xdeadbeef, 0x1);
	/* Kick start the benchmark */
//	ofs_switch_begin(shm_pa, &ofs_res);
	/* TODO: might need to hack the SMC func ID for a better name */
	if (OPTEE_SMC_RETURN_IS_RPC(ofs_res.a0)) {
#if 0
		printk("lwg:%s:catch an RPC, dump return value:\n", __func__);
		printk("lwg:a0 = %08lx\n", ofs_res.a0);
		printk("lwg:a1 = %08lx\n", ofs_res.a1);
		printk("lwg:a2 = %08lx\n", ofs_res.a2);
		printk("lwg:a3 = %08lx\n", ofs_res.a3);

		/* In our page test a1 is used for PA of allocated page */
		phys_addr_t pa = ofs_res.a1;
		int			idx = ofs_res.a2;
		void *va = ioremap(pa, PAGE_SIZE);
		smp_mb();
		printk("lwg:%s:trying to access [%08lx] mapped to [%p]\n", __func__,pa, va);
		/* It turns out that this PA can be accessed... */
		printk("lwg:%s: *(int *)va = [%08x]\n", __func__, *(int *)va);
		/* Page allocation testing code snippet */
#endif 
		printk("lwg:%s:%d:sending pg alloc request\n", __func__, __LINE__);
		ofs_pg_request(0xdeadbeef, 0x1);
		msg = recv_ofs_msg(ofs_shm);
		printk("lwg:%s:%d: receiving OP = %d\n", __func__, __LINE__, msg->op);
		printk("lwg:%s:page allocated @ [0x%lx]\n", __func__, msg->msg.page_response.pa);
		return rc; /* Testing page request only, return upon success */


		msg = recv_ofs_msg(ofs_shm);
		smp_mb();
		if (msg) 
			ofs_handle_msg(msg);
	} else {
		rc = ofs_res.a0;
	}
	/* Finish handling, returning to secure world */
#if 0 
	/* Testing code, to see if a block request can be passed back */
	msg->op = OFS_BLK_REQUEST;
	msg->msg.fs_response.blocknr = 0xdeadbeef;
	msg->msg.fs_response.rw = 0x1;
	msg->msg.fs_response.payload = NULL;
	smp_mb();

	ofs_switch_resume(&ofs_res);
#endif 


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
	printk(KERN_INFO"lwg:%s:ofs_tee@PA %16llx, ofs_tee@VA %p, ofs_tee@VA %p\n", __func__, virt_to_phys(ofs_tee), ofs_tee, (void *)(&ofs_tee));
	rc = ofs_bench();  /* kickstart */
	return 0;
}

static void __exit ofs_cleanup(void)
{
	printk(KERN_INFO "Cleaning up module.\n");
	ofs_tee=NULL;
}

module_init(ofs_init);
module_exit(ofs_cleanup);
