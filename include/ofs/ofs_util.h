#ifndef OFS_UTIL_H
#define OFS_UTIL_H


#include <linux/tee_drv.h>
#include <linux/arm-smccc.h>
#include <linux/mutex.h>
/* Dirty */
#include <ofs/optee_private.h>
#include <ofs/optee_msg.h>
#include <ofs/optee_smc.h>
#include <ofs/tee_private.h>
/* OFS msg sending */
#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include <linux/pagemap.h>
/* fs-related struct, utils */
#include <linux/fs.h>
#include <linux/slab.h>

#define OFS_FS "ext2"

extern struct tee_shm *ofs_shm;
extern struct tee_context *ofs_tee_context;
extern struct tee_device *ofs_tee;
extern struct arm_smccc_res ofs_res;
extern unsigned long return_thread;

static inline void ofs_dump_8b(void *va) {
	uint8_t *byte;
	int j;
	byte = va;
	printk("lwg:%s:%d:dump a few bytes...\n", __func__, __LINE__);
	for (j = 0; j < 8; j++) {
		printk("[%02x] ", *(byte + j));
	}
	printk("\n");
}

static inline void ofs_prep_fs_response(struct ofs_msg *msg, int op, int fd, int blocknr, phys_addr_t pa, int rw) {
	msg->op = op;
	msg->msg.fs_response.fd = fd;
	msg->msg.fs_response.blocknr = blocknr;
	msg->msg.fs_response.pa		 = pa;
	msg->msg.fs_response.rw		 = rw;
	smp_mb();
}

static inline int is_ofs_address_space(struct address_space *mapping) {
	if (mapping) {
		return test_bit(AS_OFS, &mapping->flags);
	}
	return !!mapping;
}

static inline int is_ofs_file(struct file *filp) {
	BUG_ON(IS_ERR(filp));
	return is_ofs_address_space(filp->f_mapping);
}

static inline void set_ofs_address_space(struct address_space *mapping) {
	BUG_ON(IS_ERR(mapping));
	return set_bit(AS_OFS, &mapping->flags);
}

static inline int set_ofs_file(struct file *filp) {
	struct inode *ino;
	struct super_block *sb;
	BUG_ON(IS_ERR(filp));
	ino = filp->f_inode;
	sb = ino->i_sb;
	if (!is_ofs_file(filp)) {
		if (!strcmp(sb->s_type->name, OFS_FS)) {
			struct address_space *mapping = filp->f_mapping;
			set_ofs_address_space(mapping);
			printk("lwg:%s:%d:setting up ofs file ino = %ld, flag = %08lx\n",
					__func__,
					__LINE__,
					ino->i_ino,
					mapping->flags);
			// printk("lwg:%s:%d:testing bit = [%d]\n", __func__, __LINE__, test_bit(AS_OFS, &mapping->flags));
			return 1;
		}
	}
	return 0;
}

static inline int is_mapping_ofs_fs(struct file *filp) {
	struct inode *ino;
	struct super_block *sb;
	if (IS_ERR(filp)) {
		return 0;
	}
	ino = filp->f_inode;
	sb = ino->i_sb;
	return (!strcmp(sb->s_type->name, OFS_FS));
}

static inline void ofs_tag_address_space(struct address_space *mapping) {
	// printk("lwg:%s:%d:....\n", __func__, __LINE__);
	set_bit(AS_OFS, &mapping->flags);
}

static inline void raw_ofs_switch(u32 callid, phys_addr_t shm_pa, struct arm_smccc_res *res) {
	struct optee_rpc_param param = {};
#ifdef OFS_DEBUG
	// printk("lwg:%s:[%08x]:[%08x]\n", __func__, callid, (unsigned int)shm_pa);
#endif
	param.a0 = callid;
	switch(callid) {
		case OPTEE_SMC_CALL_WITH_ARG:
		case OFS_BENCH_START:
#ifdef OFS_DEBUG
			/* lwg: These are not loaded into register due to the exeception
			 * hanler in sec world don't load them */
			param.a3 = 1;
			param.a4 = 2;
			param.a5 = 3;
			param.a6 = 4;
			//			param.a7 = 5; /* Don't touch a7 since it'll be used as clnt id */
#endif
			/* Pair the paddr of allocated shared mem into register a1 and a2
			 * The shm is used for message passing  */
			reg_pair_from_64(&param.a1, &param.a2, shm_pa);
			break;
		case OPTEE_SMC_CALL_RETURN_FROM_RPC:
			param.a1 = res->a1;
			param.a2 = res->a2;
			param.a3 = res->a3; /* lwg: be careful not to touch a3 as it is used for thread id */
#ifdef OFS_DEBUG
			printk("lwg:%s:returning to sec world thread [%lu]\n", __func__, res->a3);
#endif
			break;
		default:
			BUG();
	}
	/* Everything's set, switch! */
	arm_smccc_smc(param.a0, param.a1, param.a2, param.a3, param.a4, param.a5, param.a6, param.a7, res);
}

static inline void ofs_switch_resume(struct arm_smccc_res *res) {
#ifdef OFS_DEBUG
	printk("lwg:%s:rpc done, resume to secure world\n", __func__);
#endif
	raw_ofs_switch(OPTEE_SMC_CALL_RETURN_FROM_RPC, 0, res);
}

static inline void ofs_switch_begin(phys_addr_t shm_pa, struct arm_smccc_res *res) {
#ifdef OFS_DEBUG
	printk("lwg:%s:shm@[%08lx], enter secure world\n", __func__, (unsigned long)shm_pa);
#endif
	raw_ofs_switch(OPTEE_SMC_CALL_WITH_ARG, shm_pa, res);
}

static inline void ofs_switch(struct arm_smccc_res *res) {
	BUG_ON(IS_ERR(ofs_shm));
#ifdef OFS_DEBUG
	printk("lwg:%s:shm@[%08lx], enter secure world\n", __func__, (unsigned long)ofs_shm->paddr);
#endif
	raw_ofs_switch(OPTEE_SMC_CALL_WITH_ARG, ofs_shm->paddr, res);
}

static inline struct ofs_msg *recv_ofs_msg(struct tee_shm *shm) {
	/* TODO: add locks */
	return (struct ofs_msg *)shm->kaddr;
}

static inline struct tee_shm *alloc_ofs_shm(struct tee_context *ctx, int size) {
	if (!ctx) {
		printk("%s:no ctx??? ofs_tee_context @ [%p]\n", __func__, (void *)ofs_tee_context);
		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		ctx->teedev = ofs_tee;
		INIT_LIST_HEAD(&ctx->list_shm);
	}
	return tee_shm_alloc(ctx, size, TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	// return tee_shm_alloc(ctx, size, TEE_SHM_MAPPED);
}

static inline void free_ofs_context(struct tee_context *ctx) {
	struct tee_shm *shm;
	mutex_lock(&ctx->teedev->mutex);
	list_for_each_entry(shm, &ctx->list_shm, link)
		shm->ctx = NULL;
	mutex_unlock(&ctx->teedev->mutex);
	kfree(ctx);
}

/* TODO: add locks */
static inline void ofs_prep_pg_request(struct ofs_msg *msg, pgoff_t index, phys_addr_t from, int request, int flag) {
	msg->op = OFS_PG_REQUEST;
	msg->msg.page_request.flag  = flag;
	msg->msg.page_request.index = index;
	msg->msg.page_request.request = request;
	msg->msg.page_request.pa = from;
}

static inline void ofs_prep_pg_alloc_request(struct ofs_msg *msg, pgoff_t index, int flag) {
	return ofs_prep_pg_request(msg, index, 0, 1, flag);
}

static inline void ofs_pg_copy_request(pgoff_t index, phys_addr_t from, int direction) {
	struct ofs_msg *msg;
	phys_addr_t shm_pa;
	int rc;
	msg = recv_ofs_msg(ofs_shm);
	/* Make sure the shm is allocated before sending any messages */
	WARN_ON(!msg);
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	ofs_prep_pg_request(msg, index, from, direction, 0);
	if (direction == OFS_PG_COPY_TO_SEC) {
		printk("lwg:%s:%d:copy a page @ [0x%llx] in normal world to secure world\n", __func__, __LINE__, from);
	} else {
		printk("lwg:%s:%d:copy a page @ [0x%llx] in normal world from secure world\n", __func__, __LINE__, from);
	}
	ofs_switch_begin(shm_pa, &ofs_res);
}

/* TODO:refactor, this is really just page alloc request */
static inline void ofs_pg_request(pgoff_t index, int flag) {
	struct ofs_msg *msg;
	phys_addr_t shm_pa;
	int rc;
	msg = recv_ofs_msg(ofs_shm);
	/* Make sure the shm is allocated before sending any messages */
	WARN_ON(!msg);
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	// ofs_prep_pg_request(msg, index, 0, flag);
	ofs_prep_pg_alloc_request(msg, index, flag);
	printk("lwg:%s:%d:allocate a secure page for [0x%lx] in secure world\n", __func__, __LINE__, index);
	ofs_switch_begin(shm_pa, &ofs_res);
}

/* TODO: add locks */
static inline void ofs_prep_blk_request(struct ofs_msg *msg, sector_t block, int rw, phys_addr_t pa, int count) {
	msg->op = OFS_BLK_REQUEST;
	/* TODO: extend this to a list batch them..? */
	msg->msg.fs_response.blocknr = block;
	msg->msg.fs_response.rw = rw;
	msg->msg.fs_response.pa = pa;
	msg->msg.fs_response.fd = count;
}

static inline void ofs_blk_read_write(sector_t block, phys_addr_t pa, int rw, int count) {
	struct ofs_msg *msg;
	phys_addr_t shm_pa;
	int rc;
	msg = recv_ofs_msg(ofs_shm);
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	WARN_ON(!msg);
	WARN_ON(!pa);
	ofs_prep_blk_request(msg, block, rw, pa, count);
	ofs_switch_begin(shm_pa, &ofs_res);
}

/* TODO: add locks refactor */
static inline void ofs_blk_read_to_pa(sector_t block, phys_addr_t pa, int count) {
	struct ofs_msg *msg;
	phys_addr_t shm_pa;
	int rc;
	msg = recv_ofs_msg(ofs_shm);
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	WARN_ON(!msg);
	WARN_ON(!pa);
	ofs_prep_blk_request(msg, block, OFS_BLK_READ, pa, count);
	ofs_switch_begin(shm_pa, &ofs_res);
}

/* TODO: add locks */
static inline void ofs_blk_write_from_pa(sector_t block, phys_addr_t pa, int count) {
	return ofs_blk_read_write(block, pa, OFS_BLK_WRITE, count);
}

/* dummy, does not do world switch in this */
static inline void ofs_blk_read(struct ofs_msg *msg, sector_t block) {
	return ofs_prep_blk_request(msg, block, OFS_BLK_READ, 0, 0);
}

static inline int is_ofs_init(struct super_block *sb) {
	int ret = 0;
	if (sb) {
		ret = sb->s_flags & MS_OFS;
	}
	return ret;
}

#endif /* OFS_UTIL_H */
