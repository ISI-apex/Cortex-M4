#ifndef TEST_H
#define TEST_H

int test_trch_dma();
int test_rt_mmu();
int test_float();
int test_systick();
int test_wdts();
int test_etimer();
int test_core_rti_timer();
int test_shmem();
int test_32_mmu_access_physical_mwr(uint32_t addr_from, uint32_t addr_to, unsigned mapping_sz); //map -> write -> read test
int test_32_mmu_access_physical_wmr(uint32_t addr_from, uint32_t addr_to, unsigned mapping_sz); //write -> map -> read test
int test_mmu_mapping_swap(uint32_t addr_from_1, uint32_t addr_from_2, uint64_t addr_to, unsigned mapping_sz);

#endif // TEST_H
