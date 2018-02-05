#ifndef OFS_UTIL_H
#define OFS_UTIL_H

#define OFS_DEBUG 1

#include <linux/tee_drv.h>
#include <linux/arm-smccc.h>
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

#define OFS_FS "ext2"

extern struct tee_shm *ofs_shm;
extern struct arm_smccc_res ofs_res;


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
	BUG_ON(IS_ERR(filp));
	struct inode *ino;
	struct super_block *sb;
	ino = filp->f_inode;
	sb = ino->i_sb;
	if (!is_ofs_file(filp)) {
		if (!strcmp(sb->s_type->name, OFS_FS)) {
			printk("lwg:%s:%d:setting up ofs file ino = %ld\n", 
					__func__, 
					__LINE__,
					ino->i_ino);
			set_ofs_address_space(filp->f_mapping);
			return 1;
		}
	}
	return 0;
}


static inline void ofs_tag_address_space(struct address_space *mapping) {
	set_bit(AS_OFS, &mapping->flags);
}

static inline void raw_ofs_switch(u32 callid, phys_addr_t shm_pa, struct arm_smccc_res *res) {
	struct optee_rpc_param param = {};
#ifdef OFS_DEBUG
	printk("lwg:%s:[%08x]:[%08x]\n", __func__, callid, (unsigned int)shm_pa);
#endif
	param.a0 = callid;
	switch(callid) {
		case OPTEE_SMC_CALL_WITH_ARG:
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
	return (struct ofs_msg *)shm->kaddr;
}



static inline void ofs_prep_pg_request(struct ofs_msg *msg, pgoff_t index, int flag) {
	msg->op = OFS_PG_REQUEST;
	msg->msg.page_request.flag  = flag;
	msg->msg.page_request.index = index;
	msg->msg.page_request.request = 0;
}

static inline void ofs_pg_request(pgoff_t index, int flag) {
	struct ofs_msg *msg;
	phys_addr_t shm_pa;
	int rc;
	msg = recv_ofs_msg(ofs_shm);
	/* This is only to make sure the shm is allocated before sending any messages */
	WARN_ON(!msg); 	
	rc = tee_shm_get_pa(ofs_shm, 0, &shm_pa);
	ofs_prep_pg_request(msg, index, flag);
//	ofs_switch_resume(&ofs_res);
	printk("lwg:%s:%d:allocate a secure page for [0x%lx] in secure world\n", __func__, __LINE__, index);
	ofs_switch_begin(shm_pa, &ofs_res);
}

static inline void ofs_blk_request(struct ofs_msg *msg, sector_t block, int rw) {
	msg->op = OFS_BLK_REQUEST;
	msg->msg.fs_response.blocknr = block;
	msg->msg.fs_response.rw = rw;
	msg->msg.fs_response.payload = NULL;
}

static inline void ofs_blk_read(struct ofs_msg *msg, sector_t block) {
	return ofs_blk_request(msg, block, 0x1);
}





#endif /* OFS_UTIL_H */
