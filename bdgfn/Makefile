.DEFAULT: all

#NETMAP_SRC ?= $(realpath ../netmap)
NETMAP_SRC ?= $(realpath /root/netmap-release)
NETMAP_KSYM := $(NETMAP_SRC)/LINUX/Module.symvers
KSRC ?=  /lib/modules/`uname -r`/build

INCLUDE_PATH := $(PWD)/include

M := `pwd`
CROSS_MAKE := $(MAKE) -j1 DESTDIR=
EXTRA_CFLAGS := -I$(INCLUDE_PATH) -I$(NETMAP_SRC)/sys -I$(NETMAP_SRC)/LINUX
EXTRA_CFLAGS += -Wno-format -Wno-unused-function
CONFIG_DEBUG_INFO ?= y

BDGFNCTL_OBJS := bdgfnctl.o
BDGFN_OBJS := bdgfn.o

obj-$(CONFIG_XEN_NETDEV_BACKEND) := mac_static.o ip_static.o
#learning-y := learn.o
mac_static-y := mac-static.o
ip_static-y := ip-static.o

all: bdgfn.o libbdgfn.so bdgfnctl
	make KBUILD_EXTRA_SYMBOLS=$(NETMAP_KSYM) -C $(KSRC) \
		EXTRA_CFLAGS='$(EXTRA_CFLAGS)' M=$(PWD) CONFIG_DEBUG_INFO=y

bdgfn.o: bdgfn.c
	$(CC) $(EXTRA_CFLAGS) -c -fPIC -o bdgfn.o bdgfn.c

bdgfnctl.o: bdgfn.o bdgfnctl.c $(INCLUDE_PATH)/bdgfn_user.h
	$(CC) $(EXTRA_CFLAGS) -c -fPIC -o bdgfnctl.o bdgfnctl.c

bdgfnctl: bdgfnctl.o
	$(CC) $(BDGFN_OBJS) $(BDGFNCTL_OBJS) -o $@

libbdgfn.so: bdgfn.o
	$(CC) -shared $(LDFLAGS) $(BDGFN_OBJS) -o $@

clean:
	make -C $(KSRC) M=$(PWD) clean
	rm -rf bdgfnctl libbdgfn.so *.o
