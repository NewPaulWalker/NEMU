SRCS-y += src/nemu-main.c
DIRS-y = src/cpu src/monitor src/utils
DIRS-$(CONFIG_MODE_SYSTEM) += src/memory
SRCS-BLACKLIST-$(CONFIG_TARGET_AM) += src/monitor/ui.c src/monitor/watchpoint.c src/utils/expr.c

ifdef CONFIG_TARGET_SHARE
SHARE = 1
else
LIBS += $(if $(CONFIG_TARGET_AM),,-lreadline -ldl -pie)
endif

ifdef mainargs
ASFLAGS += -DBIN_PATH=\"$(mainargs)\"
endif
SRCS-$(CONFIG_TARGET_AM) += src/am-bin.S
.PHONY: src/am-bin.S