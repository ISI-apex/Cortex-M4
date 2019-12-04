#ifndef SUBSYS_H
#define SUBSYS_H

// Note: when changing, remember to change name array in subsys.c
typedef enum { // bitmask
    SUBSYS_INVALID  = 0,
    SUBSYS_TRCH     = 0x1,
    SUBSYS_RTPS_R52 = 0x2,
    SUBSYS_RTPS_A53 = 0x4,
    SUBSYS_HPPS     = 0x8,
    NUM_SUBSYSS     = 4
} subsys_t;

enum sw_subsys { /* logical subsystem: grouping of CPU resources */
   SW_SUBSYS_TRCH = 0,
   SW_SUBSYS_RTPS_R52_LOCKSTEP,
   SW_SUBSYS_RTPS_R52_SMP,
   SW_SUBSYS_RTPS_R52_SPLIT_0,
   SW_SUBSYS_RTPS_R52_SPLIT_1,
   SW_SUBSYS_RTPS_A53,
   SW_SUBSYS_HPPS_SMP,
   SW_SUBSYS_HPPS_SMP_CL0,
   SW_SUBSYS_HPPS_SMP_CL1,
   NUM_SW_SUBSYS
};

enum sw_comp { /* a software component runs on a logical subsystem */
   SW_COMP_SSW,
   SW_COMP_ATF,
   SW_COMP_APP,
};

/* An identifier for an entity to which a resource can be allocated */
#define OWNER(sw_subsys, sw_comp) (((sw_subsys) << 8) | (sw_comp))

typedef enum { // bitmask
#define COMP_CPUS_SHIFT_TRCH 0
    COMP_CPU_TRCH       = 0x0001,

#define COMP_CPUS_SHIFT_RTPS     1
#define COMP_CPUS_SHIFT_RTPS_R52 1
    COMP_CPU_RTPS_R52_0 = 0x0002,
    COMP_CPU_RTPS_R52_1 = 0x0004,

#define COMP_CPUS_SHIFT_RTPS_A53 3
    COMP_CPU_RTPS_A53_0 = 0x0008,

#define COMP_CPUS_SHIFT_HPPS 4
#define COMP_CPUS_SHIFT_HPPS_CL1 8
    COMP_CPU_HPPS_0     = 0x0010,
    COMP_CPU_HPPS_1     = 0x0020,
    COMP_CPU_HPPS_2     = 0x0040,
    COMP_CPU_HPPS_3     = 0x0080,
    COMP_CPU_HPPS_4     = 0x0100,
    COMP_CPU_HPPS_5     = 0x0200,
    COMP_CPU_HPPS_6     = 0x0400,
    COMP_CPU_HPPS_7     = 0x0800,
} comp_t;

#define COMP_CPUS_TRCH COMP_CPU_TRCH
#define COMP_CPUS_RTPS_R52 (COMP_CPU_RTPS_R52_0 | COMP_CPU_RTPS_R52_1)
#define COMP_CPUS_RTPS_A53 (COMP_CPU_RTPS_A53_0)
#define COMP_CPUS_RTPS (COMP_CPUS_RTPS_R52 | COMP_CPUS_RTPS_A53)
#define COMP_CPUS_HPPS_CL0 \
        (COMP_CPU_HPPS_0 | COMP_CPU_HPPS_1 |  COMP_CPU_HPPS_2 | COMP_CPU_HPPS_3)
#define COMP_CPUS_HPPS_CL1 \
         (COMP_CPU_HPPS_4 | COMP_CPU_HPPS_5 |  COMP_CPU_HPPS_6 | COMP_CPU_HPPS_7)
#define COMP_CPUS_HPPS \
        (COMP_CPUS_HPPS_CL0 | COMP_CPUS_HPPS_CL1)

#define TRCH_NUM_CORES     1
#define RTPS_R52_NUM_CORES 2
#define RTPS_A53_NUM_CORES 1
#define HPPS_NUM_CORES     8

#define NUM_CORES \
      (TRCH_NUM_CORES + RTPS_R52_NUM_CORES + RTPS_A53_NUM_CORES + HPPS_NUM_CORES)

struct cpu_group {
    subsys_t subsys;
    comp_t cpu_set;
    unsigned num_cores;
};

enum cpu_group_id {
   CPU_GROUP_TRCH = 0,
   CPU_GROUP_RTPS_R52,
   CPU_GROUP_RTPS_R52_0,
   CPU_GROUP_RTPS_R52_1,
   CPU_GROUP_RTPS_A53,
   CPU_GROUP_HPPS,
   NUM_CPU_GROUPS,
};

const struct cpu_group *subsys_cpu_group(enum cpu_group_id gid);

const char *subsys_name(subsys_t subsys);

#endif // SUBSYS_H
