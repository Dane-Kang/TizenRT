#include $(TOPDIR)/make/common.mk

SRCDIR = libsodium/src/libsodium
PREFIX := libsodium_

INCS := -I$(SRCDIR)/include \
        -I$(SRCDIR)/include/sodium \
        -Iport/include \
        -Iport/include/sodium

CFLAGS  := -DCONFIGURED
CFLAGS  += -DNATIVE_LITTLE_ENDIAN
CFLAGS  += -DHAVE_WEAK_SYMBOLS
CFLAGS  += -D__STDC_LIMIT_MACROS
CFLAGS  += -D__STDC_CONSTANT_MACROS
CFLAGS  += $(INCS)

SRCS = $(shell find $(SRCDIR) -name '*.c')
OBJS = $(SRCS:.c=.o)

# === libsodium 정적 라이브러리 생성 ===
LIBSODIUM_A := libsodium.a

.PHONY: all clean

all: $(LIBSODIUM_A)

$(LIBSODIUM_A): $(OBJS)
	@echo "  AR    $@"
	@$(AR) rcs $@ $(OBJS)

# ★ 핵심: 이 규칙이 있어야 CFLAGS(INCS 포함)가 반드시 반영됨
%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@rm -f $(OBJS) $(LIBSODIUM_A)