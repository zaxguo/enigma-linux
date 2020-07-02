
#define ENIGMA_MAX_PART 100
struct enigma_part_tbl {
	int count;
	// by the time header of hd_struct has been included already
	struct hd_struct* part_list[ENIGMA_MAX_PART];
};

