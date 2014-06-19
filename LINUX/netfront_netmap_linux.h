/*
 *          xen-netfront (linux)
 *
 *   file: netfront_netmap_linux.h
 *
 *          NEC Europe Ltd. PROPRIETARY INFORMATION
 *
 * This software is supplied under the terms of a license agreement
 * or nondisclosure agreement with NEC Europe Ltd. and may not be
 * copied or disclosed except in accordance with the terms of that
 * agreement. The software and its source code contain valuable trade
 * secrets and confidential information which have to be maintained in
 * confidence.
 * Any unauthorized publication, transfer to third parties or duplication
 * of the object or source code - either totally or in part – is
 * prohibited.
 *
 *      Copyright (c) 2014 NEC Europe Ltd. All Rights Reserved.
 *
 * Authors: Joao Martins <joao.martins@neclab.eu>
 *
 * NEC Europe Ltd. DISCLAIMS ALL WARRANTIES, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE AND THE WARRANTY AGAINST LATENT
 * DEFECTS, WITH RESPECT TO THE PROGRAM AND THE ACCOMPANYING
 * DOCUMENTATION.
 *
 * No Liability For Consequential Damages IN NO EVENT SHALL NEC Europe
 * Ltd., NEC Corporation OR ANY OF ITS SUBSIDIARIES BE LIABLE FOR ANY
 * DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS
 * OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF INFORMATION, OR
 * OTHER PECUNIARY LOSS AND INDIRECT, CONSEQUENTIAL, INCIDENTAL,
 * ECONOMIC OR PUNITIVE DAMAGES) ARISING OUT OF THE USE OF OR INABILITY
 * TO USE THIS PROGRAM, EVEN IF NEC Europe Ltd. HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 *     THIS HEADER MAY NOT BE EXTRACTED OR MODIFIED IN ANY WAY.
 */
#ifndef __XEN_NETFRONT_NETMAP_H__
#define __XEN_NETFRONT_NETMAP_H__

#include <netmap.h>
#include <bsd_glue.h>
#include <netmap/netmap_kern.h>

#define SOFTC_T		 netfront_info

#if NETMAP_API > 3
#include <netmap_mem2.h>
#define VIFGREF(r,i) 	r->slot[i].ptr
#else
#define VIFGREF(r,i) 	r->slot[i].len
#endif

#include <linux/log2.h>

#define NETFRONT_BUF_SIZE	 2048

#define NETFRONT_BUF(t, index) \
	(t + index * NETFRONT_BUF_SIZE)

#define NETFRONT_TDH(na_priv) \
	((struct netmap_info*)na_priv)->stat->txhead

#define	NETMAP_RING_NEXT(r, i)				\
	((i)+1 == (r)->num_slots ? 0 : (i) + 1 )

static inline uint32_t
nm_ring_space(struct netmap_ring *ring)
{
		int ret = ring->tail - ring->cur;
		if (ret < 0)
				ret += ring->num_slots;
		return ret;
}

struct netfront_info;

struct gnttab_map_entry {
	/* the guest address of the other domains page*/
	unsigned long host_addr;
	grant_handle_t handle;
};

struct netmap_gnt {
	uint32_t *ring_grefs;
	uint32_t *bufs_grefs;
	struct gnttab_map_grant_ref *ring_map;
	struct gnttab_map_entry *ring_gnt;
	struct gnttab_map_grant_ref *bufs_map;
	struct gnttab_map_entry *bufs_gnt;
	uint32_t nr_ring_grefs;
	uint32_t nr_bufs_grefs;
	char  *bufs_base;
	wait_queue_head_t wait;
};

struct netfront_csb {
	/* these are only used by the guest */
	uint16_t txcur;
	uint16_t txhead;
	uint8_t guest_need_txkick;
	uint8_t guest_need_rxkick;

	/* these are mostly changed by the event channels (host) */
	uint8_t host_need_rxkick;
	uint8_t host_need_txkick;
};

struct netmap_info {
	struct netmap_gnt txd;
	struct netmap_gnt rxd;

	uint16_t num_tx_rings;
	uint16_t num_rx_rings;

	struct netmap_ring *tx_rings;
	struct netmap_ring *rx_rings;

	struct netfront_csb  *stat;

	evtchn_port_t tx_evtchn;
	unsigned int tx_irq;

	evtchn_port_t rx_evtchn;
	unsigned int rx_irq;

	domid_t  dom;
	uint32_t reset_timeout;
	const char *nodename;
	char *backend;
};

static int netfront_netmap_reg(struct netmap_adapter *na, int onoff)
{
	struct ifnet *ifp = na->ifp;
	int error = 0;

	if (!(ifp)->flags & IFF_UP) {
		D("Interface is down!");
		return EINVAL;
	}

	if (na == NULL)
		return EINVAL;

	if (onoff) {
		nm_set_native_flags(na);
		netif_carrier_on(ifp);
	} else {
		nm_clear_native_flags(na);
		netif_carrier_off(ifp);
	}
	return error;
}

int netfront_netmap_rxsync(struct netmap_kring *kring, int flags)
{
    struct netmap_adapter *na = kring->na;
	struct netfront_info *nf = netdev_priv(na->ifp);
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */ //j,
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = nm_rxsync_prologue(kring);
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;

	if (head > lim)
		return netmap_ring_reinit(kring);

	/*
	 * First part: import newly received packets.
	 */
	if (netmap_no_pendintr || force_update) {
		/* extract buffers from the rx queue, stop at most one
		 * slot before nr_hwcur (stop_i)
		 */
		uint16_t slot_flags = kring->nkr_slot_flags;
		u_int stop_i = nm_prev(kring->nr_hwcur, lim);
		struct netmap_info *ni = (struct netmap_info *) nf->na_priv;
		struct netmap_ring *nfr = ni->rx_rings;
		struct netfront_csb *stat = ni->stat;
		u_int cur = nfr->cur;
		u_int j = nm_ring_space(nfr);

		nm_i = kring->nr_hwtail; /* first empty slot in the receive ring */
		for (n = 0; nm_i != stop_i; n++) {
			void *addr = NMB(&ring->slot[nm_i]);
			char *nfp;
			int len;

			/* we only check the address here on generic rx rings */
			if (addr == netmap_buffer_base) { /* Bad buffer */
				return netmap_ring_reinit(kring);
			}

			/* device specific */
			if (!stat->host_need_rxkick || !j)
				break;

			len = nfr->slot[cur].len;
			nfp = NETFRONT_BUF(ni->rxd.bufs_base, cur);
			memcpy(addr, nfp, len);
			cur = NETMAP_RING_NEXT(nfr, cur);

			ring->slot[nm_i].len = len;
			ring->slot[nm_i].flags = slot_flags;
			nm_i = nm_next(nm_i, lim);
			n++;
			j--;
		}
		if (n) {
			kring->nr_hwtail = nm_i;
		}
		if (stat->host_need_rxkick) {
			if (n)
				nfr->head = nfr->cur = cur;
			stat->host_need_rxkick = 0;
			notify_remote_via_irq(ni->rx_irq);
		}
		kring->nr_kflags &= ~NKR_PENDINTR;
	}

	/*
	 * Second part: skip past packets that userspace has released.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		/* Userspace has released some packets. */
		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];

			slot->flags &= ~NS_BUF_CHANGED;
			nm_i = nm_next(nm_i, lim);
		}
		kring->nr_hwcur = head;
	}
	/* tell userspace that there might be new packets. */
	nm_rxsync_finalize(kring);

	return 0;
}

static int netfront_netmap_xmit(struct SOFTC_T *np, char *buf, u_int len)
{
	struct netmap_info *ni = np->na_priv;
	struct netmap_ring *ring = ni->tx_rings;
	struct netfront_csb *stat = ni->stat;
	u_int cur = stat->txcur;
	struct netmap_slot *slot = &ring->slot[cur];
	char *p = NETFRONT_BUF(ni->txd.bufs_base, ring->cur);

	if (nm_ring_empty(ring))
		return 0;

	memcpy(p, buf, len);
	slot->len = len;
	cur = NETMAP_RING_NEXT(ring, cur);
	stat->txhead = stat->txcur = cur;

	if (stat->host_need_txkick) {
		ring->head = ring->cur = stat->txcur;
		stat->guest_need_txkick = 1;
		stat->host_need_txkick = 0;
		notify_remote_via_irq(ni->tx_irq);
	}

	return 1;
}

int netfront_netmap_txsync(struct netmap_kring *kring, int flags)
{
    struct netmap_adapter *na = kring->na;
	struct ifnet *ifp = na->ifp;
	struct netfront_info *nf = netdev_priv(na->ifp);
	struct netmap_info *ni = nf->na_priv;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */
	u_int nic_i;	/* index into the NIC ring */
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	int reclaim_tx;

	rmb();

	if (!netif_carrier_ok(ifp)) {
		goto out;
	}

	/*
	 * First part: process new packets to send.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {	/* we have new packets to send */
		nic_i = netmap_idx_k2n(kring, nm_i);
		while (nm_i != head) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			u_int len = slot->len;
			void *addr = NMB(slot);

			/* device-specific */
			if (!netfront_netmap_xmit(nf, addr, len))
				break;

			slot->flags &= ~(NS_REPORT | NS_BUF_CHANGED);
			nm_i = nm_next(nm_i, lim);
		}

		/* Update hwcur to the next slot to transmit. */
		kring->nr_hwcur = nm_i; /* not head, we could break early */
	}

	if (ni->stat->guest_need_txkick) {
		reclaim_tx = 0;
	} else if (flags & NAF_FORCE_RECLAIM) {
		reclaim_tx = 1; /* forced reclaim */
	} else if (!nm_kr_txempty(kring)) {
		reclaim_tx = 0; /* have buffers, no reclaim */
	} else {
		reclaim_tx = 1;
	}

	/*
	 * Second, reclaim completed buffers
	 */
	if (reclaim_tx) {
		nic_i = NETFRONT_TDH(nf->na_priv);
		if (nic_i >= kring->nkr_num_slots) {
			nic_i -= kring->nkr_num_slots;
		}

		kring->nr_hwtail = nm_prev(netmap_idx_n2k(kring, nic_i), lim);
	}
	ND("tx #%d, hwtail = %d", n, kring->nr_hwtail);

out:
	nm_txsync_finalize(kring);
	return 0;
}

void netfront_netmap_attach(struct SOFTC_T *sc)
{
	struct netmap_adapter na;
	struct netmap_info *priv = sc->na_priv;

	bzero(&na, sizeof(na));

	na.ifp = sc->netdev;
	BUG_ON(na.ifp->priv_flags & IFCAP_NETMAP);

	na.num_tx_desc = priv->tx_rings->num_slots;
	na.num_rx_desc = priv->rx_rings->num_slots;
	na.num_tx_rings = 1;
	na.num_rx_rings = 1;
	na.nm_txsync   = netfront_netmap_txsync;
	na.nm_rxsync   = netfront_netmap_rxsync;
	na.nm_register = netfront_netmap_reg;

	pr_info("netfront_netmap %d %d txd %d %d rxd\n", na.num_tx_desc,
		na.num_rx_desc, na.num_tx_rings, na.num_rx_rings);

	spin_lock_bh(&sc->rx_lock);
	spin_lock_irq(&sc->tx_lock);
	notify_remote_via_irq(priv->rx_irq);
	notify_remote_via_irq(priv->tx_irq);
	spin_unlock_irq(&sc->tx_lock);
	spin_unlock_bh(&sc->rx_lock);

	netmap_attach(&na);
}

static irqreturn_t netfront_tx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct netfront_info *np = netdev_priv(dev);
	struct netmap_info *ni = np->na_priv;
	struct netfront_csb *stat;
	unsigned long flags;

	spin_lock_irqsave(&np->tx_lock, flags);
	if (likely(netif_carrier_ok(dev))) {
		stat = ni->stat;
		stat->host_need_txkick = 1;
		stat->guest_need_txkick = 0;
		netmap_rx_irq(dev, 0, NULL);
	}
	spin_unlock_irqrestore(&np->tx_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t netfront_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct netfront_info *np = netdev_priv(dev);
	struct netmap_info *ni = np->na_priv;
	unsigned long flags;
	u_int work_done;
	struct netfront_csb *stat;

	spin_lock_irqsave(&np->rx_lock, flags);
	if (likely(netif_carrier_ok(dev))) {
		stat = ni->stat;
		stat->host_need_rxkick = 1;
		stat->guest_need_rxkick = 0;
		netmap_rx_irq(dev, 0, &work_done);
	}
	spin_unlock_irqrestore(&np->rx_lock, flags);

	return IRQ_HANDLED;
}

static inline uint32_t pow2(uint16_t order)
{
	uint32_t val = 1, t = order;
	if (t == 0)
		return 1;

	while (t) {
		val *= 2;
		--t;
	}
	return val;
}

static inline int order(int nr_grefs)
{
	int pgo = ilog2(nr_grefs);
	while (pow2(pgo) < nr_grefs) {
		pgo++;
	}
 	return pgo;
}

#define __alloc_pages(order) __get_free_pages(GFP_ATOMIC | __GFP_ZERO, order)

#define map_ops_new(nrefs,addr,op,pte)	\
	addr = __alloc_pages(order(nrefs)); \
	op = vmalloc(nr_grefs * sizeof(struct gnttab_map_grant_ref)); \
	pte = vmalloc(nr_grefs * sizeof(struct gnttab_map_entry))

static int gnttab_map(struct netmap_gnt *rdesc, domid_t dom, int ring)
{
	int i;
	int err = 0, gnt_err = 0;
	unsigned long addr = 0;
	int nr_grefs = 0;
	struct gnttab_map_grant_ref *op = NULL;
	struct gnttab_map_entry *pte = NULL;
	uint32_t *grefs = NULL;

	if (!ring) {
		nr_grefs = rdesc->nr_ring_grefs;
		map_ops_new(nr_grefs, addr, op, pte);
		nr_grefs--;
		rdesc->ring_map = op;
	 	rdesc->ring_gnt = pte;
		grefs = rdesc->ring_grefs;
	} else {
		nr_grefs = rdesc->nr_bufs_grefs;
		map_ops_new(nr_grefs, addr, op, pte);
		rdesc->bufs_map = op;
	 	rdesc->bufs_gnt = pte;
		grefs = rdesc->bufs_grefs;
	}

	dom = 0; // XXX network stub domains

	for (i = 0; i < nr_grefs; ++i) {
			op[i].ref   = (grant_ref_t) grefs[i];
			op[i].dom   = (domid_t) dom;
			op[i].flags = GNTMAP_host_map;
			op[i].host_addr = addr + PAGE_SIZE * i;

			pr_debug("map flags %d ref %d host_addr %d",
					op[i].flags, op[i].ref, op[i].host_addr);
	};

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
							op, nr_grefs)) {
		return -EINVAL;
	}

	for (i = 0; i < nr_grefs; i++) {
			if (op[i].status != GNTST_okay) {
					err = op[i].status;
					D("map %d not ok. status %d",
							op[i].ref, op[i].status);
					++gnt_err;
					continue;
			}

			pte[i].host_addr = op[i].host_addr;
			pte[i].handle = op[i].handle;
			pr_debug("map ok. handle %u addr %u", op[i].handle,
							op[i].host_addr);
			rmb();
	}

	if (gnt_err)
		pr_info("\t%d map errors", gnt_err);
	return 0;
}

static int gnttab_unmap(struct netmap_gnt *rdesc)
{
	int i, j = 0;
	struct gnttab_map_entry *ring_gnt = rdesc->ring_gnt;
	struct gnttab_map_entry *bufs_gnt = rdesc->bufs_gnt;
	struct gnttab_unmap_grant_ref op[rdesc->nr_ring_grefs +
			rdesc->nr_bufs_grefs];

	for (i = 0; i < rdesc->nr_ring_grefs; ++i, ++j) {
		op[j].host_addr = ring_gnt[i].host_addr;
		op[j].handle = ring_gnt[i].handle;
		op[j].dev_bus_addr = 0;

		ND("unmap ref %d host_addr %x",
			ring_gnt[i].handle, ring_gnt[i].host_addr);
	};

	for (i = 0; i < rdesc->nr_bufs_grefs; ++i, ++j) {
		op[j].host_addr = bufs_gnt[i].host_addr;
		op[j].handle = bufs_gnt[i].handle;
		op[j].dev_bus_addr = 0;

		ND("unmap ref %d host_addr %x",
			op[j].handle, bufs_op[i].host_addr);
	};

	D("Unmapping %d refs", rdesc->nr_ring_grefs + rdesc->nr_bufs_grefs);
	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
				op, rdesc->nr_ring_grefs + rdesc->nr_bufs_grefs))
		BUG();

	vfree(rdesc->ring_grefs);
	vfree(rdesc->bufs_grefs);
	return 0;
}

static int xenbus_write_evtchn(struct SOFTC_T *info)
{
	struct netmap_info *priv = info->na_priv;
	struct xenbus_transaction xbt;
	int err;
	char *message = NULL;

 again:
	err = xenbus_transaction_start(&xbt);
	err = xenbus_printf(xbt, priv->nodename, "event-channel-tx",
					"%u", priv->tx_evtchn);
	if (err) {
		message = "Writing event-channel-tx";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, priv->nodename, "event-channel-rx",
					"%u", priv->rx_evtchn);
	if (err) {
		message = "Writing event-channel-rx";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		message = "completing transaction";
		goto error;
	}
	goto done;

 abort_transaction:
	vfree(message);
	err = xenbus_transaction_end(xbt, 1);
	goto error;

 done:
	return 0;
 error:
	vfree(message);
	return -EFAULT;
}

static int xenbus_read_ring_grefs(struct xenbus_device *xbdev,
				struct SOFTC_T *sc)
{
	int i, err;
	char path[256];
	struct netmap_info *priv = sc->na_priv;

	err = xenbus_scanf(XBT_NIL, priv->nodename, "tx-ring-refs",
					"%d", &priv->txd.nr_ring_grefs);

	priv->txd.ring_grefs = vmalloc(1 + priv->txd.nr_ring_grefs
					* sizeof(uint32_t));

	// Read the tx-ring-refs
	for (i = 0; i < priv->txd.nr_ring_grefs; i++) {
		snprintf(path, sizeof(path), "tx-ring-ref%u", i);
		err = xenbus_scanf(XBT_NIL, priv->nodename, path,
					"%d", &priv->txd.ring_grefs[i]);
	}

	err = xenbus_scanf(XBT_NIL, priv->nodename, "rx-ring-refs",
					"%d", &priv->rxd.nr_ring_grefs);


	priv->rxd.ring_grefs = vmalloc(1 + priv->rxd.nr_ring_grefs
					* sizeof(uint32_t));

	// Read the rx-ring-refs
	for (i = 0; i < priv->rxd.nr_ring_grefs; i++) {
		snprintf(path, sizeof(path), "rx-ring-ref%u", i);
		err = xenbus_scanf(XBT_NIL, priv->nodename, path,
					"%d", &priv->rxd.ring_grefs[i]);
	}

	return 0;
}

static int xenbus_read_bufs_grefs(struct netmap_ring *ring, struct netmap_gnt *rdesc)
{
	int i, j;

	rdesc->nr_bufs_grefs = ring->num_slots/2;
	rdesc->bufs_grefs = vmalloc(1 + rdesc->nr_bufs_grefs * sizeof(uint32_t));

	for (i = 0, j = 0; i < ring->num_slots; i++) {
		if (VIFGREF(ring, i) != 0) {
			rdesc->bufs_grefs[j++] = VIFGREF(ring, i);
		}
	}
	rdesc->nr_bufs_grefs = j;

	return 0;
}

static int setup_netfront_netmap(struct xenbus_device *dev, struct SOFTC_T *sc)
{
	struct netmap_info *priv;
	int err, dom;
	int tx_desc, rx_desc;

	priv = vmalloc(sizeof(*priv));
	sc->na_priv = priv;

	xenbus_scanf(XBT_NIL, dev->otherend, "backend-id", "%d", &dom);
	xenbus_scanf(XBT_NIL, dev->otherend, "feature-netmap-tx-desc", "%d", &tx_desc);
	xenbus_scanf(XBT_NIL, dev->otherend, "feature-netmap-rx-desc", "%d", &rx_desc);
	priv->dom = dom;
	priv->nodename = dev->nodename;

	pr_debug("reading event-channel-tx");
	err = xenbus_alloc_evtchn(dev, &priv->tx_evtchn);
	if (err)
		goto fail;

	err = bind_evtchn_to_irqhandler(priv->tx_evtchn, netfront_tx_interrupt,
					0, sc->netdev->name, sc->netdev);
	priv->tx_irq = err;

	pr_debug("reading event-channel-rx");
	err = xenbus_alloc_evtchn(dev, &priv->rx_evtchn);
	if (err)
		goto rx_fail;

	err = bind_evtchn_to_irqhandler(priv->rx_evtchn, netfront_rx_interrupt,
					0, sc->netdev->name, sc->netdev);

	priv->rx_irq = err;

	xenbus_read_ring_grefs(dev, sc);

	gnttab_map(&priv->txd, priv->dom, 0);
	priv->tx_rings = (struct netmap_ring*) priv->txd.ring_gnt[0].host_addr;
	while (priv->tx_rings->num_slots != tx_desc) {
		rmb();
	}

	gnttab_map(&priv->rxd, priv->dom, 0);
	priv->rx_rings = (struct netmap_ring*) priv->rxd.ring_gnt[0].host_addr;
	while (priv->rx_rings->num_slots != rx_desc) {
		rmb();
	}

	xenbus_read_bufs_grefs(priv->tx_rings, &priv->txd);
	gnttab_map(&priv->txd, priv->dom, 1);
	priv->txd.bufs_base = (char *) priv->txd.bufs_gnt[0].host_addr;

	xenbus_read_bufs_grefs(priv->rx_rings, &priv->rxd);
	gnttab_map(&priv->rxd, priv->dom, 1);
	priv->rxd.bufs_base = (char *) priv->rxd.bufs_gnt[0].host_addr;

	pr_info("mapping %d/%d port %d/%d\n", priv->rxd.nr_bufs_grefs,
			priv->txd.nr_bufs_grefs, priv->rx_irq, priv->tx_irq);

	xenbus_write_evtchn(sc);

	priv->stat = vmalloc(sizeof(struct netfront_csb));
	memset(priv->stat, 0, sizeof(struct netfront_csb));
	priv->stat->host_need_txkick = 1;
	priv->stat->guest_need_txkick = 0;
	priv->stat->host_need_rxkick = 0;
	priv->stat->guest_need_rxkick = 1;
	priv->stat->txcur = priv->tx_rings->cur;
	priv->stat->txhead = priv->tx_rings->head;

	return 0;
rx_fail:
	unbind_from_irqhandler(priv->tx_evtchn, sc->netdev);
fail:
	return err;
}

void xennet_netmap_disconnect(struct SOFTC_T *sc)
{
	struct netmap_info *ni = sc->na_priv;

	unbind_from_irqhandler(ni->rx_irq, sc->netdev);
	unbind_from_irqhandler(ni->tx_irq, sc->netdev);

	netmap_detach(sc->netdev);
	
	/* Unmapping rings currently gives a panic in
	 * unregister_netdev */
	//gnttab_unmap(&ni->rxd);
	//gnttab_unmap(&ni->txd);

	vfree(ni->stat);
	vfree(ni);
}

#endif /* __XEN_NETFRONT_NETMAP_H__ */
