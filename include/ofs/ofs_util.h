#ifndef OFS_UTIL_H
#define OFS_UTIL_H

#define OFS_DEBUG 1

#include <linux/tee_drv.h>

struct ofs_msg;

inline struct ofs_msg *recv_ofs_msg(struct tee_shm *shm) {
	return (struct ofs_msg *)shm->kaddr;
}


/* TODO: a better name, this function is used to kick start the benchmark */
inline void ofs_switch(u32 callid, phys_addr_t shm_pa, struct arm_smccc_res *res) {
	struct optee_rpc_param param = {};
#ifdef OFS_DEBUG
	printk("lwg:%s:[%08lx]:[%08lx]\n", __func__, callid, (unsigned int)shm_pa);
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
			param.a7 = 5;
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
			printk("lwg:%s:returning to sec world thread [%d]\n", __func__, res.a3);
#endif
			break;
		default:
			BUG();
	}
	/* Everything's set, switch! */
	arm_smccc_smc(param.a0, param.a1, param.a2, param.a3, param.a4, param.a5, param.a6, param.a7, res);
}


inline void ofs_switch_resume(struct arm_smccc_res *res) {
#ifdef OFS_DEBUG
	printk("lwg:%s:rpc done, resume to secure world\n", __func__);
#endif
	ofs_switch(OPTEE_SMC_CALL_RETURN_FROM_RPC, 0, res);
}

inline void ofs_switch_begin(phys_addr_t shm_pa, struct arm_smccc_res *res) {
#ifdef OFS_DEBUG
	printk("lwg:%s:kick start the benchmark, used for first-time entry\n", __func__);
#endif
	ofs_switch(OPTEE_SMC_CALL_WITH_ARG, shm_pa, res);
}

#endif
