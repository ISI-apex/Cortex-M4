#ifndef MMU_H
#define MMU_H

// PT addr has to be 32-bit since TRCH must be able to populate PTs
int mmu_init(uint32_t pt_addr, unsigned ptspace);
int mmu_context_create();
int mmu_context_destroy(unsigned ctx);
int mmu_map(unsigned ctx, uint64_t vaddr, uint64_t paddr, unsigned sz);
int mmu_unmap(unsigned ctx, uint64_t vaddr, uint64_t paddr, unsigned sz);
int mmu_stream_create(unsigned master, unsigned ctx);
int mmu_stream_destroy(unsigned stream);
void mmu_enable();
void mmu_disable();

#endif // MMU_H
