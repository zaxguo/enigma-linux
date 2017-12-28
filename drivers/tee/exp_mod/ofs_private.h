#ifndef OFS_UTIL_H
#define OFS_UTIL_H 

#include "../tee_private.h" /* struct tee_shm */
inline struct ofs_msg *recv_ofs_msg(struct tee_shm *shm) {
	return (struct ofs_msg *)shm->kaddr;
}	


/* TODO: a better name, this function is used to kick start the benchmark */
inline void ofs_switch(phys_addr_t shm_pa, struct arm_smccc_res *res) {
	printk("lwg:%s:preparing to do world switch\n", __func__);
	struct optee_rpc_param param;
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
	/* Everything's set, switch! */
	arm_smccc_smc(param.a0, param.a1, param.a2, param.a3, param.a4, param.a5, param.a6, param.a7, res);
}

#endif
