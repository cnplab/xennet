XEN_NETBACK := xen-netback/xen-netback
XEN_NETFRONT := xen-netfront/xen-netfront
.DEFAULT: all

KSRC ?=  /lib/modules/`uname -r`/build
NETMAP_SRC ?= $(realpath ../netmap)
NETMAP_KSYM := $(NETMAP_SRC)/LINUX/Module.symvers

ifndef NETMAP_SRC
$(error "Modify NETMAP_SRC to match your dir structure")
endif

ifndef KSRC
$(error "Modify KSRC (kernel src dir) to match your dir structure")
endif

#
## Taken from netmap building process
LIN_VER = $(shell V=linux/version.h; G=. ; \
	[ -f $(KSRC)/include/$${V} ] || G=generated/uapi ;\
	grep LINUX_VERSION_CODE $(KSRC)/include/$${G}/linux/version.h | \
	awk '{printf "%03x%02x", $$3/256, $$3%256} ')
PATCHES := $(shell \
	    cd $(PWD)/patches; ls diff--* | awk -v v=$(LIN_VER) -F -- \
	    '{ if ((!$$3 || $$3 <= v) && (!$$4 || v < $$4)) print $$0; }')
#

M := `pwd`
CROSS_MAKE := $(MAKE) -j1 DESTDIR=
EXTRA_CFLAGS := -I$(PWD) -I$(NETMAP_SRC)/LINUX -I$(NETMAP_SRC)/sys/dev/netmap -I$(NETMAP_SRC)/sys/net -I$(NETMAP_SRC)/sys/dev -I$(PWD)/xen-netback
EXTRA_CFLAGS += -Wno-format -Wno-unused-function -DCONFIG_NETMAP

CONFIG_NETMAP_XENBUS ?= y
CONFIG_DEBUG_INFO 	 ?= y
CONFIG_NETMAP		 ?= y

obj-m := $(XEN_NETBACK).o $(XEN_NETFRONT).o
$(XEN_NETBACK)-y := xen-netback/netback.o xen-netback/xenbus.o xen-netback/interface.o

all:

drivers:
	make KBUILD_EXTRA_SYMBOLS=$(NETMAP_KSYM) -C $(KSRC) \
		EXTRA_CFLAGS='$(EXTRA_CFLAGS)' M=$(PWD) \
		CONFIG_DEBUG_INFO=y CONFIG_NETMAP_XENBUS=y CONFIG_NETMAP=y

$(NETMAP_KSYM):
	(cd $(NETMAP_SRC)/LINUX && \
		export KSRC=$(KSRC) && \
		$(CROSS_MAKE) )

get-drivers:
	rm -rf xen-netback/ xen-netfront/
	cp -aR $(KSRC)/drivers/net/xen-netback .
	mkdir -p xen-netfront
	cp -aR $(KSRC)/drivers/net/xen-netfront.c xen-netfront
	(cd .. ; \
	for i in $(PATCHES) ; \
	do patch -p1 < LINUX/patches/$$i; \
	done ; );
	cd -;


prepare: get-drivers
	(cd $(NETMAP_SRC) ;\
	git reset --hard ;\
	patch -p1 < $(wildcard ../LINUX/patches/0001-*) ;\
	patch -p1 < $(wildcard ../LINUX/patches/0002-*) ;\
	cd LINUX ;\
	rm -rf patches/diff--virtio_net.c--30b00--99999 ;\
	export KSRC=$(KSRC);\
	$(CROSS_MAKE) get-drivers;\
	cd $(NETMAP_SRC);)
	#cp ../LINUX/ixgbe/ixgbe.h LINUX/ixgbe/ixgbe.h)

all:  $(NETMAP_KSYM) drivers

clean:
	make -C $(KSRC) M=$(PWD) clean
	make -C $(KSRC) M=$(PWD)/xen-netback clean

cleanall: clean
	(cd $(NETMAP_SRC)/LINUX && \
		export KSRC=$(KSRC) && \
		$(CROSS_MAKE) clean )
