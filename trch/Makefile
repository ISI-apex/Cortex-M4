include ../Makefile.common

# List all possible test flags here
CONFIG_FLAGS = \
	TEST_FLOAT \
	TEST_SYSTICK \
	TEST_WDTS \
	TEST_TRCH_DMA \
	TEST_TRCH_DMA_CB \
	TEST_RT_MMU \
	TEST_ETIMER \
	TEST_RTI_TIMER \
	TEST_SHMEM \
	TEST_32_MMU_ACCESS_PHYSICAL \
	TEST_MMU_MAPPING_SWAP \
	CONFIG_SYSTICK \
	CONFIG_SLEEP_TIMER \
	CONFIG_HPPS_TRCH_MAILBOX \
	CONFIG_HPPS_TRCH_MAILBOX_ATF \
	CONFIG_HPPS_TRCH_MAILBOX_SSW \
	CONFIG_RTPS_TRCH_MAILBOX \
	CONFIG_RTPS_TRCH_MAILBOX_PSCI \
	CONFIG_TRCH_WDT \
	CONFIG_RTPS_TRCH_SHMEM \
	CONFIG_HPPS_TRCH_SHMEM \
	CONFIG_HPPS_TRCH_SHMEM_SSW \
	CONFIG_RTPS_R52_WDT \
	CONFIG_RTPS_A53_WDT \
	CONFIG_HPPS_WDT \
	CONFIG_TRCH_DMA \
	CONFIG_RT_MMU \

include Makefile.defconfig
include Makefile.config

ifeq ($(strip $(CONFIG_SLEEP_TIMER)),1)
ifneq ($(strip $(CONFIG_SYSTICK)),1)
$(error CONFIG_SLEEP_TIMER requires a timer to be enabled)
endif
endif

# Most tests are standalone, but some are not
ifeq ($(strip $(TEST_RT_MMU)),1)
ifneq ($(strip $(CONFIG_RT_MMU)),1)
$(error TEST_RT_MMU requires CONFIG_RT_MMU)
endif
endif
ifeq ($(strip $(TEST_WDTS)),1)
ifneq ($(call cfg-or,$(CONFIG_TRCH_WDT) $(CONFIG_RTPS_R52_WDT) $(CONFIG_HPPS_WDT)),1)
$(error TEST_WDTS requires one of CONFIG_*_WDT)
# TODO: remove this dependency by factoring ISRs into a separate file
endif
endif

CONFIG_ARGS = $(foreach m,$(CONFIG_FLAGS),-D$(m)=$($(m)))
CONFIG_ARGS += -DCONFIG_CONSOLE__$(CONFIG_CONSOLE)

CPU_FLAGS = -mcpu=cortex-m4 -mthumb 
FLOAT_FLAG = -mfloat-abi=hard 
AOPS = --warn --fatal-warnings $(CPU_FLAGS) $(FLOAT_FLAG)
INC_FLAGS = -I. -Itests $(foreach d,$(SUBDIRS),-I../$(d)) \

COPS = \
	-Wall -Werror \
	-O0 -g \
	$(CPU_FLAGS) $(FLOAT_FLAG) \
	-nostdlib -nostartfiles -ffreestanding \
	-DUART_BASE=LSIO_UART0_BASE \
	-DPRINTF_SUPPORT_FLOAT \
	-DPRINTF_NO_DOUBLE_WORKAROUND \
	$(CONFIG_ARGS) \
	$(INC_FLAGS) \

# -z max-page-size: alignment of program headers in the ELF file
#	removes large gaps (0x10000) in the ELF file (see: readelf -l)
LDFLAGS = \
	-z max-page-size=4 \

VPATH = ..
# vectors.o and isrs.o must be first
OBJS = \
       vectors.o \
       isrs.o \
       drivers/cortex-m4.o \
       drivers/dma.o \
       drivers/etimer.o \
       drivers/mailbox.o \
       drivers/mmu.o \
       drivers/ns16550.o \
       drivers/nvic.o \
       drivers/rti-timer.o \
       drivers/smc.o \
       drivers/systick.o \
       drivers/wdt.o \
       lib/balloc.o \
       lib/bit.o \
       lib/command.o \
       lib/intc.o \
       lib/llist.o \
       lib/mailbox-link.o \
       lib/mem.o \
       lib/memfs.o \
       lib/object.o \
       lib/panic.o \
       lib/printf.o \
       lib/sha256.o \
       lib/shmem.o \
       lib/shmem-link.o \
       lib/sleep.o \
       lib/str.o \
       plat/board.o \
       plat/console.o \
       boot.o \
       isr.o \
       main.o \
       psci.o \
       reset.o \
       server.o \
       syscfg.o \
       subsys.o \

ifeq ($(call cfg-or,$(CONFIG_TRCH_WDT) $(CONFIG_RTPS_R52_WDT) $(CONFIG_HPPS_WDT)),1)
OBJS += watchdog.o
endif
ifeq ($(strip $(CONFIG_RT_MMU)),1)
OBJS += mmus.o
endif
ifeq ($(call cfg-or,$(CONFIG_TRCH_DMA) $(TEST_TRCH_DMA)),1)
OBJS += dmas.o
endif
ifeq ($(call cfg-or,\
	$(CONFIG_HPPS_TRCH_MAILBOX) \
	$(CONFIG_HPPS_TRCH_MAILBOX_ATF) \
	$(CONFIG_HPPS_TRCH_MAILBOX_SSW) \
	$(CONFIG_HPPS_TRCH_MAILBOX_PSCI) \
	$(CONFIG_RTPS_TRCH_MAILBOX)),1)
OBJS += mailbox-isr.o
endif

ifeq ($(strip $(TEST_FLOAT)),1)
OBJS += tests/float.o
endif
ifeq ($(strip $(TEST_SYSTICK)),1)
OBJS += tests/systick.o
endif
ifeq ($(strip $(TEST_TRCH_DMA)),1)
OBJS += tests/dma.o
endif
ifeq ($(strip $(TEST_RT_MMU)),1)
OBJS += tests/mmu.o
endif
ifeq ($(strip $(TEST_32_MMU_ACCESS_PHYSICAL)),1)
OBJS += tests/mmu.o
OBJS += mmus.o
endif
ifeq ($(strip $(TEST_MMU_MAPPING_SWAP)),1)
OBJS += tests/mmu.o
OBJS += mmus.o
endif
ifeq ($(strip $(TEST_WDTS)),1)
OBJS += tests/wdt.o
endif
ifeq ($(strip $(TEST_ETIMER)),1)
OBJS += tests/etimer.o
endif
ifeq ($(strip $(TEST_RTI_TIMER)),1)
OBJS += test/test-rti-timer.o tests/rti-timer.o
endif
ifeq ($(strip $(TEST_SHMEM)),1)
OBJS += tests/shmem.o
endif

TARGET=trch

.DEFAULT_GOAL := $(BLDDIR)/$(TARGET).bin

clean:
	rm -rf $(BLDDIR) vectors.s vectors.s.gen isr.c isr.c.gen

isrs: irqmap genisr.py Makefile Makefile.defconfig Makefile.config
	./genisr.py -v $(INC_FLAGS) $(CONFIG_ARGS) --irqmap $< vectors.s.gen isr.c.gen
.INTERMEDIATE: isrs
vectors.s.gen: isrs
isr.c.gen: isrs
vectors.s: vectors.s.gen isrs
	cp $< $@
isr.c: isr.c.gen isrs
	cp $< $@

$(BLDDIR)/%.o : %.s | dirs
	$(CROSS_COMPILE)as $(AOPS) $< -o $@

$(BLDDIR)/%.o : %.c | dirs
	$(CROSS_COMPILE)gcc -MMD $(COPS) -c $< -o $@

$(BLDDIR)/$(TARGET).dbg.elf: trch.ld $(addprefix $(BLDDIR)/,$(OBJS))
	$(CROSS_COMPILE)ld $(LDFLAGS) -o $@ -T $^

$(BLDDIR)/$(TARGET).elf : $(BLDDIR)/$(TARGET).dbg.elf
	$(CROSS_COMPILE)strip -x -s -R .comment -R '.ARM.*' -o $@ $<

$(BLDDIR)/$(TARGET).bin : $(BLDDIR)/$(TARGET).elf
	$(CROSS_COMPILE)objcopy -O binary $< $@

$(addprefix $(BLDDIR)/,$(OBJS)) : Makefile Makefile.defconfig Makefile.config

# Empty config means all settings come from defconfig
Makefile.config:
	touch $@

-include $(addprefix $(BLDDIR)/,$(OBJS:.o=.d))
