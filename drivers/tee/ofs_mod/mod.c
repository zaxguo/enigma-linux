/* lwg: This kernel module serves two goals:
 * 1: kick starts the TA in secure world
 * 2: handle the request issued by secure world */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>

#include <linux/slab.h> // mem
#include <linux/tee_drv.h> // tee
#include "../tee_private.h" /* struct tee_shm */
#include "../optee/optee_private.h" /* optee_rpc_param */
/* #include "../optee/optee_smc.h" //optee */
#include <ofs/optee_smc.h>
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <asm/io.h>  // virt_to_phys
#include <linux/syscalls.h> // syscall
#include <linux/fs.h> /* filp_open */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/proc_fs.h> /* userspace interaction */
#include <linux/unistd.h>
#include <linux/mm.h> /* ioremap */
#include <linux/gfp.h> /* alloc_page */
#include <asm/page.h>
#include <linux/highmem.h> /* kmap_atomic */

#include <ofs/ofs_msg.h> /* struct ofs_msg */
#include <ofs/ofs_util.h>  /* some utility functions */
#include <ofs/ofs_opcode.h>
#include "ofs_handler.h"
#include <linux/dma-mapping.h>
#include "ofs_syscall.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("LWG");
MODULE_DESCRIPTION("OFS Normal World Handler Interacting with TEE");

extern struct tee_device *ofs_tee;
extern struct tee_shm *tee_shm_alloc(struct tee_context *ctx, size_t size, u32 flags);
extern int tee_shm_get_pa(struct tee_shm *shm, size_t off, phys_addr_t *pa);
extern int	ofs_mkdir(const char *, int);
extern struct tee_shm *ofs_shm; /* Global message passing shared memory */
extern struct arm_smccc_res ofs_res;

struct page *write_buf;
unsigned long return_thread = 0;

struct files_struct ofs_files = {
	.count		= ATOMIC_INIT(1),
	.fdt		= &ofs_files.fdtab,
	.fdtab		= {
		.max_fds	= NR_OPEN_DEFAULT,
		.fd		= &ofs_files.fd_array[0],
		.close_on_exec	= ofs_files.close_on_exec_init,
		.open_fds	= ofs_files.open_fds_init,
		.full_fds_bits	= ofs_files.full_fds_bits_init,
	},
	.file_lock	= __SPIN_LOCK_UNLOCKED(ofs_files.file_lock),
};

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
#ifdef OFS_DEBUG
	printk("lwg:%s:opcode = %d\n", __func__, opcode);
#endif
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

static void ofs_bench_start(phys_addr_t shm_pa, struct arm_smccc_res *res) {
#ifdef OFS_DEBUG
	printk("lwg:%s:%d:kick start ofs bench\n", __func__, __LINE__);
#endif
	raw_ofs_switch(OFS_BENCH_START, shm_pa, res);
}

static int ofs_bench(void *data) {
//	struct arm_smccc_res *res;
	struct tee_context *ctx;
	int rc;
	phys_addr_t shm_pa;
	struct ofs_msg *msg;
	int op;

	/* Init */
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
	/* ofs_pg_request(0xdeadbeef, 0x1); */
	/* Kick start the benchmark */
	ofs_bench_start(shm_pa, &ofs_res);
	/* TODO: may change this to indicate the end of the benchmark */
	while(OPTEE_SMC_RETURN_IS_RPC(ofs_res.a0)) {
		return_thread = ofs_res.a3;
		printk("lwg:%s:%d:RPC from sec thread [%d], start normal world fs\n", __func__, __LINE__, ofs_res.a3);
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
		printk("lwg:%s:%d:sending pg alloc request\n", __func__, __LINE__);
		ofs_pg_request(0xdeadbeef, 0x1);
		msg = recv_ofs_msg(ofs_shm);
		printk("lwg:%s:%d: receiving OP = %d\n", __func__, __LINE__, msg->op);
		printk("lwg:%s:page allocated @ [0x%lx]\n", __func__, msg->msg.page_response.pa);
		return rc; /* Testing page request only, return upon success */
#endif
		msg = recv_ofs_msg(ofs_shm);
		smp_mb();
		if (msg) {
			ofs_handle_msg(msg);
			/* finish handling */
			/* ofs_switch_resume(&ofs_res); */
		}
		/* One considertion of not swithing here is that kthread is async,
		 * which may lead to premature resume */
		/* ofs_switch_resume(&ofs_res); */
	}

	{
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

static void read_file(void) {
	struct file *f;
	struct inode *ino;
//	char buf[128];
	f = filp_open("/mnt/ext2/ofs", O_RDONLY, 0);
	if (IS_ERR(f)) {
		printk("lwg:%s:cannot read file\n", __func__);
		return;
	}
	ino = f->f_inode;

	printk("lwg:%s:%d:%lu:%s\n", __func__, __LINE__, f->f_inode->i_ino, ino->i_sb->s_type->name);
	return;
}

static void test_page(void)  {
	struct page *page;
	int dump_size = 0x2;
	int i;
	page = alloc_page(GFP_KERNEL);
	if (page) {
		void *addr = page_address(page);
		for(i = 0;  i < dump_size; i++) {
			printk("[%02x] ", *(uint8_t *)(addr + i));
		}
		printk("\n");
		clear_page(addr);
		for(i = 0;  i < dump_size; i++) {
			printk("[%02x] ", *(uint8_t *)(addr + i));
		}
		printk("\n");
		printk("page @ va [%p], data page @ va [%p]\n", (void *)page, addr);
		addr = kmap_atomic(page);
		printk("page @ va [%p]\n", addr);
		kunmap_atomic(addr);
		printk("page highmem = %d\n", PageHighMem(page) ? 1 : 0);
	}
}

static void test_cma(void) {
	void *vaddr;
	struct device *dev;
	dma_addr_t dma_addr;
	uint8_t *byte;
	unsigned long size = 1024 * 4096;
	int i = 0;
	dev = &(ofs_tee->dev);
	vaddr = dma_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	byte = (uint8_t *)vaddr;
	if (!vaddr) {
		printk("XXXXXXX failed to alloc %08lx bytes\n", size);
		return;
	}
	printk("success cma alloc @ [%p]\n", vaddr);
	printk("dump bytes...\n");
	for(i = size - 8; i < size; i++) {
		printk("[%02x] ", *(byte + i));
	}

	return ;
}

static void test(void) {
	char p[10];
	struct file *file;
	int fd = 0;
	int read = 0;
	file = fcheck_files(&ofs_files, fd);
	if (file) {
		read = kernel_read(file, 0, p, 5);
		if (read > 0) {
			p[read] = '\0';
			printk("read out [%s]\n", p);
		}
	} else {
		WARN_ON(1);
	}
	return;
}

/* More convenient to control debugging and kickstart the benchmark
 * op =
 * 1: kickstart benchmark
 * other: debugging different facilities */
static ssize_t ofs_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	int op = 0;
	char ops[128];
	struct task_struct *tsk;
	struct ofs_msg *msg;
	if (copy_from_user(ops, buf, count)) {
		return -EFAULT;
	}
	sscanf(ops, "%d\n", &op);
	printk("lwg:%s:%d:op = %d\n", __func__, __LINE__, op);
	switch (op) {
		case 1:
			printk("lwg:%s:%d:kick start benchmark\n", __func__, __LINE__);
			ofs_switch(&ofs_res);

			smp_mb();
			msg = recv_ofs_msg(ofs_shm);
			ofs_handle_msg(msg);
			ofs_switch_resume(&ofs_res);
			break;
		case 2:
//			read_file();
			ofs_switch(&ofs_res);
			smp_mb();
			msg = recv_ofs_msg(ofs_shm);
			ofs_handle_msg(msg);
			break;
		case 3:
			/* test code zone */
			test();
			break;
		case 4:
			/* start the benchmark, vfs does not seem to work
			 * in its *own* calling context, we start another
			 * kthread to do the job */
			tsk = kthread_run(ofs_bench, NULL, "ofs_bench");
			break;
		default:
			break;
	}
	return count;
}

static inline void ofs_print_help(struct seq_file *file) {

	seq_printf(file, "------------OFS-----------\n");
	seq_printf(file, "Usage:\n");
	seq_printf(file, "echo [x] > /proc/ofs\n");
	seq_printf(file, "x = 1-3: different tests\n");
	seq_printf(file, "x = 4: kickstart the benchmark\n");

}

static int ofs_show(struct seq_file *file, void *v) {
	seq_printf(file, "lwg:%s:%d:opened\n", __func__, __LINE__);
	ofs_print_help(file);
	return 0;
}

static int ofs_file_open(struct inode *inode, struct file *filp) {
	return single_open(filp, ofs_show, NULL);
}

static const struct file_operations ofs_procfs_ops = {
	/* note that an open must correspond to one release, vice versa. Otherwise bad things will happen  */
	.open  = ofs_file_open,
	.write = ofs_proc_write,
	.read = seq_read,
	.release = seq_release,
};

static int remove_ofs_procfs(struct proc_dir_entry *proc) {
	proc_remove(proc);
	return 0;
}

static int init_ofs_procfs(void) {
	struct proc_dir_entry *ofs;
	ofs = proc_create("ofs", 0444, NULL, &ofs_procfs_ops);
	if (!ofs)
		return -ENOMEM;
	return 0;
}

static void init_rw_buf(void) {
	void *addr;
	write_buf = alloc_page(GFP_KERNEL);
	addr = kmap_atomic(write_buf);
	memset(addr, 0x63, PAGE_SIZE);
	kunmap_atomic(addr);
	printk("read/write buf initialized!\n");
}


static int __init ofs_init(void)
{
	struct tee_context *ctx;
	int rc;
	phys_addr_t shm_pa;

	/* Init */
	init_ofs_procfs();
	init_rw_buf();
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
	if (!ofs_tee) {
		printk(KERN_ERR"lwg:%s:cannot find tee class!\n",__func__);
		return 1;
	}
	printk(KERN_INFO"lwg:%s:OFS init sucess:----------------------------------\n",__func__);
	printk(KERN_INFO"lwg:%s:ofs_tee@PA[%08llx], ofs_tee@VA[%p], ofs_tee@VA[%p]\n", __func__, virt_to_phys(ofs_tee), ofs_tee, (void *)(&ofs_tee));
//	ofs_pg_request(0x0, 1);
//	rc = ofs_bench();  /* kickstart */
	return 0;
}

static void __exit ofs_cleanup(void)
{
	printk(KERN_INFO "Cleaning up module.\n");
	ofs_tee=NULL;
	/* never do this */
	remove_ofs_procfs(NULL);
}

module_init(ofs_init);
module_exit(ofs_cleanup);

