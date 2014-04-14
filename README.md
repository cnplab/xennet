Xen Network
===========

This repository consists of a fast network backend to accelerate Xen packet I/O for ClickOS virtual machines and netmap-based applications in Linux VMs.


System Requirements
===================

Linux kernel version >= 3.6 installed
	* Kernel sources that *exactly* correspond to your installed kernel
	* Kernel headers/symbols for the kernel version you are against
	* Compilation of the target NIC (only ixgbe supprted for now), backend and frontend as modules
	* Example .config:
	
	CONFIG_IXGBE=m
	CONFIG_IXGBE_HWMON=y
	CONFIG_IXGBE_DCA=y
	CONFIG_IXGBEVF=m
	CONFIG_XEN_NETDEV_BACKEND=m
	CONFIG_XEN_NETDEV_FRONTEND=m

Backend
=======

Grab the sources:

```
$ git clone https://github.com/cnplab/xennet
$ git submodule update --init
```

If your kernel isn't built already, you will need at least
to do a `prepare`:

```
$ cp /boot/config-`uname -r` /usr/src/linux-source-X.X.X/.config
$ cd /usr/src/linux-source-X.X.X
$ make prepare
$ cd -
```

By default it compiles everything (netmap + netback/netfront)

```
$ make KSRC=/usr/src/linux-source-X.X.X prepare
$ make KSRC=/usr/src/linux-headers-X.X.X
```

Then just load the modules

```
$ insmod ../netmap/LINUX/netmap_lin.ko;
$ insmod xen-netback/xen-netback.ko;
```

Copy the Xen hotplug script:

```
$ cp ../scripts/vif-vale /etc/xen/scripts/
```

Finally, the hotplug scripts require a netmap tool called `vale-ctl`. 
We need to build it and either copy to your ```/usr/local/bin``` or 
export it to your ```PATH```

```
$ cd ../netmap/examples
$ make	
$ export PATH=$PATH:`pwd`/../netmap/examples
```

Backend (Linux kernel ```<= 3.11```)
================================

The network backend provided with kernels <= 3.11 cannot be removed. 
Before inserting the patched netback driver you need to blacklist 
the old module at startup time, and then reboot the machine; only 
then can you insert our modified netback.


```
$ vim /etc/modprobe.d/blacklist.conf
blacklist xen-netfront
blacklist xen-netback
```

Usage
=====

To use our backend you will require a compatible frontend. 
You can find it [here](https://github.com/cnplab/click-os/ (built with 
```CONFIG_NETMAP=y```) or the Linux Frontend you just build in the steps 
above. Using it just insert use a bridge name as ```valeX``` meaning X your
bridge name. Finally, change your hotplug script in your Xen domain
configuration. Overall your ```vif``` entry will look like this:

```
vif = ['mac=00:15:17:15:5d:74,bridge=vale0,script=/etc/xen/scripts/vif-vale']
```

Linux Frontend
--------------

First boot a domain without network:

```
$ xl create path-to-xen-config
$ xl console linux
```

On your domU, add the following entry to ```/etc/modprobe.d/blacklist.conf```:

```
blacklist xen-netfront
```

Remove the xen-netfront:

```
rmmod xen-netfront;
```

Then you just load your patched xen-netfront:

```
$ insmod path-to-xennet/netmap/LINUX/netmap_lin.ko
$ insmod path-to-xennet/LINUX/xen-netfront/xen-netfront.ko
$ ifconfig eth0 up
$ cd ../netmap/examples
$ ./pkt-gen -i eth0 -f rx -b 1024
```

Afterwards on your Domain 0 try running the packet generator:

```
$ cd path-to-xennet/netmap/examples
$ ./pkt-gen -w 2 -i eth0 -f tx -l 60 -b 1024
```
