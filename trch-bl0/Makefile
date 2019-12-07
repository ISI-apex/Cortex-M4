include ../Makefile.common

# List boolean test and config flags here (always defined)
CONFIG_FLAGS = \

# List value-typed config options here (defined only if non-empty)
CONFIG_OPTS = \
	CONFIG_CFG_ADDR_0 \
	CONFIG_CFG_ADDR_1 \
	CONFIG_CFG_ADDR_2 \
	CONFIG_RELOC_ADDR \
	CONFIG_UART_BASE \
	CONFIG_UART_BAUDRATE \

include Makefile.defconfig
include Makefile.config

CONFIG_ARGS = $(foreach m,$(CONFIG_FLAGS),-D$(m)=$($(m)))
CONFIG_ARGS += $(foreach m,$(CONFIG_OPTS),$(if $($(m)), -D$(m)=$($(m))))

ifneq ($(strip $(CONFIG_CONSOLE)),)
CONFIG_ARGS += -DCONFIG_CONSOLE
endif

ifneq ($(CONFIG_BOOT_SELECT),)
CONFIG_ARGS += -DCONFIG_BOOT_SELECT='0$(patsubst %,|%,$(CONFIG_BOOT_SELECT))'
endif # CONFIG_BOOT_SELECT

CPU_FLAGS = -mcpu=cortex-m4 -mthumb 
FLOAT_FLAG = -mfloat-abi=hard 
AOPS = --warn --fatal-warnings $(CPU_FLAGS) $(FLOAT_FLAG)
INC_FLAGS = -I. -Itests $(foreach d,$(SUBDIRS),-I../$(d)) \

COPS = \
	-Wall -Werror \
	-fpic \
	-O0 -g \
	$(CPU_FLAGS) $(FLOAT_FLAG) \
	-nostdlib -nostartfiles -ffreestanding \
	-DUART_BASE=LSIO_UART0_BASE \
	-DPRINTF_SUPPORT_FLOAT \
	-DPRINTF_NO_DOUBLE_WORKAROUND \
	$(CONFIG_ARGS) \
	$(INC_FLAGS) \
	$(CFLAGS) \

# -z max-page-size: alignment of program headers in the ELF file
#	removes large gaps (0x10000) in the ELF file (see: readelf -l)
LDFLAGS = \
	-z max-page-size=4 \

VPATH = ..
# vectors.o and isrs.o must be first
OBJS = \
       vectors.o \
       main.o \
       relocate_code.o \
       isrs.o \
       drivers/cortex-m4.o \
       drivers/ns16550.o \
       drivers/smc.o \
       drivers/systick.o \
       lib/ecc.o \
       lib/intc.o \
       lib/mem.o \
       lib/object.o \
       lib/panic.o \
       lib/sha256.o \
       lib/str.o \
       plat/board.o \
       main-relocated.o \
       sections.o \

ifneq ($(strip $(CONFIG_CONSOLE)),)
OBJS += lib/printf.o
endif # CONFIG_CONSOLE

ifeq ($(strip $(CONFIG_CONSOLE)),NS16550)
OBJS += plat/console/ns16550.o
endif # CONFIG_CONSOLE

TARGET=trch-bl0

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

$(BLDDIR)/$(TARGET).cpp.ld: $(TARGET).ld | dirs
	$(CROSS_COMPILE)gcc $(COPS) -E -P -C -nostdinc -x assembler-with-cpp \
		-o $@ $<

$(BLDDIR)/$(TARGET).dbg.elf: $(BLDDIR)/$(TARGET).cpp.ld \
			     $(addprefix $(BLDDIR)/,$(OBJS))
	$(CROSS_COMPILE)ld $(LDFLAGS) -o $@ -T $^

$(BLDDIR)/$(TARGET).elf : $(BLDDIR)/$(TARGET).dbg.elf
	$(CROSS_COMPILE)strip -x -s -R .comment -R '.ARM.*' -o $@ $<

$(BLDDIR)/$(TARGET).bin : $(BLDDIR)/$(TARGET).elf
	$(CROSS_COMPILE)objcopy -O binary $< $@

$(addprefix $(BLDDIR)/,$(OBJS)) : Makefile Makefile.defconfig Makefile.config

# Empty config means all settings come from defconfig
Makefile.config:
	touch $@

%.s: %.S
	$(CROSS_COMPILE)gcc $(COPS) -E -o $@ $<

-include $(addprefix $(BLDDIR)/,$(OBJS:.o=.d))
