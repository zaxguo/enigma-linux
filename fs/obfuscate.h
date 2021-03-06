#ifndef _OBFUSCATE_H_
#define _OBFUSCATE_H_
#include <linux/arm-smccc.h>
#include <ofs/optee_smc.h>
#define CURR_K get_enigma_k()
#define TARGET_APP "a.out"
#define ALT_APP "alt.out"
#define MAX_BUDDY_NAME 128
#define O_OURS		00000004
#define O_SHUFFLED	040000000

// #define ENIGMA_DEBUG

#ifdef ENIGMA_DEBUG
#define lwg_printk(fmt, ...)	printk("lwg:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define lwg_printk(fmt, ...) (void)0
#endif

extern struct arm_smccc_res enigma_res;
extern int enigma_k;

static inline int get_enigma_k(void) {
	return enigma_k;
}

static inline void enigma_switch(void) {
	arm_smccc_smc(OFS_NOTIFY_IMG, 0, 0, 0, 0, 0, 0, 0, &enigma_res);
}

#endif
