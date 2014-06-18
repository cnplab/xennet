/*
 *          xen-netback (linux)
 *
 *   file: netback_netmap_linux.h
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

#ifndef __XEN_NETBACK_NETMAP_H__
#define __XEN_NETBACK_NETMAP_H__

#include <netmap.h>
#include <bsd_glue.h>
#include <netmap/netmap_kern.h>

#include "xen-netback/common.h"

#include <xen/interface/memory.h>
#include <asm/xen/page.h>

#define SOFTC_T xenvif

#include <netmap_mem2.h>
#define VIFGREF(r,i) 	r->slot[i].ptr

#define NETMAP_RING_SIZE	sizeof(struct netmap_ring)
#define RING_SLOTS(n)		n * sizeof(struct netmap_slot)
#define RING_NR_PAGES(desc) 	(NETMAP_RING_SIZE + RING_SLOTS(desc))/PAGE_SIZE
#define BUFS_NR_PAGES(ring)	(ring->slot[0].len * ring->num_slots)/PAGE_SIZE

static int default_tx_desc = 1024;
module_param(default_tx_desc, int, 0644);
static int default_rx_desc = 1024;
module_param(default_rx_desc, int, 0644);

struct netmap_vpwrap_adapter {
	struct netmap_adapter *up;
	u_int guest_need_rxkick;
	u_int guest_need_txkick;
};

static int xenvif_notify(struct netmap_adapter *na, u_int ring_nr, enum txrx tx, int flags);
static int xenvif_netmap_txsync(struct netmap_adapter *na, u_int ring_nr, int flags);
static int xenvif_netmap_rxsync(struct netmap_adapter *na, u_int ring_nr, int flags);

static void xenvif_netmap_map(struct SOFTC_T *info, struct netmap_adapter *na);
static void xenvif_netmap_unmap(struct SOFTC_T *vif);

static int xenbus_write_grefs(struct xenbus_device *dev,
				struct xenbus_transaction *bus,	struct xenvif *vif);
static uint16_t nm_ring_pages(struct netmap_kring *kring, int ndesc,
				struct page* pages[]);
static uint16_t  nm_buf_pages(struct netmap_kring *kring, int ndesc,
				struct netmap_mem_d *nm_mem, struct page* pages[]);


void xenvif_netmap_up(struct SOFTC_T *vif)
{
	netmap_enable_all_rings(vif->dev);
	enable_irq(vif->tx_irq);
	if (vif->tx_irq != vif->rx_irq)
		enable_irq(vif->rx_irq);
}

void xenvif_netmap_down(struct SOFTC_T *vif)
{
	netmap_disable_all_rings(vif->dev);
	disable_irq(vif->tx_irq);
	if (vif->tx_irq != vif->rx_irq)
		disable_irq(vif->rx_irq);
}

static int xenvif_netmap_reg(struct netmap_adapter *hwna, int onoff)
{
	struct ifnet *ifp = hwna->ifp;
	struct SOFTC_T *adapter = netdev_priv(ifp);

	if (!(ifp)->flags & IFF_UP) {
		D("Interface is down!");
		return EINVAL;
	}

	/* enable or disable flags and callbacks in na and ifp */
	if (onoff) {
		struct netmap_bwrap_adapter *bna = hwna->na_private;
		struct netmap_vp_adapter *vpna = &bna->up;
		struct netmap_vpwrap_adapter *vwna;

		xenvif_netmap_map(adapter, &vpna->up);
		nm_set_native_flags(hwna);

		vwna = malloc(sizeof(struct netmap_vpwrap_adapter),
			M_DEVBUF, M_NOWAIT | M_ZERO);
		vwna->guest_need_rxkick = 1;
		vwna->guest_need_txkick = 0;
		vwna->up = hwna;
		vpna->up.na_private = vwna;
		vpna->up.nm_notify = xenvif_notify;
	} else {
		nm_clear_native_flags(hwna);
	}

	return (0);
}

int xenvif_netmap_irq(struct SOFTC_T *vif, int tx)
{
	struct netmap_adapter *hwna = NA(vif->dev);
	struct netmap_bwrap_adapter *bna = hwna->na_private;
	struct netmap_adapter *na = &bna->up.up;
	struct netmap_vpwrap_adapter *vwna = na->na_private;

	if (tx) {
		nm_txsync_prologue(&na->tx_rings[0]);
		na->nm_txsync(na, 0, 0);
		vwna->guest_need_txkick = 0;
		if (vif->tx_irq)
			notify_remote_via_irq(vif->tx_irq);
	} else {
		na->nm_rxsync(na, 0, 0);
		vwna->guest_need_rxkick = 1;
	}

	return 0;
}

int xenvif_netmap_irqsched(struct SOFTC_T *vif, int tx)
{
	struct netmap_adapter *hwna = NA(vif->dev);
	struct netmap_bwrap_adapter *bna = hwna->na_private;
	struct netmap_vp_adapter *vpna = &bna->up;
	struct netmap_adapter *na = &vpna->up;
	struct netmap_vpwrap_adapter *vwna = na->na_private;

	if (!netif_carrier_ok(vif->dev)) {
		return 1;
	}

	if (vpna->na_bdg == NULL) {
		return 1;
	}

	if (tx) {
		vwna->guest_need_txkick = 1;
	}

	wake_up(&vif->wq);
	return 0;
}

static inline
int rx_work_todo(struct netmap_vpwrap_adapter *vwna)
{
	return (vwna->guest_need_rxkick == 0);
}

static inline
int tx_work_todo(struct netmap_vpwrap_adapter *vwna)
{
	return (vwna->guest_need_txkick == 1);
}

int xenvif_netmap_kthread(void *data)
{
	struct SOFTC_T *vif = data;
	struct netmap_adapter *hwna = NA(vif->dev);
	struct netmap_bwrap_adapter *bna = hwna->na_private;
	struct netmap_adapter *na = &bna->up.up;
	struct netmap_vpwrap_adapter *vwna = na->na_private;

	while (!kthread_should_stop()) {
		wait_event_interruptible(vif->wq,
					 rx_work_todo(vwna) ||
					 tx_work_todo(vwna) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		if (tx_work_todo(vwna))
			xenvif_netmap_irq(vif, 1);

		if (rx_work_todo(vwna))
			xenvif_netmap_irq(vif, 0);

		cond_resched();
	}

	return 0;
}

int xenvif_netmap_txsync(struct netmap_adapter *na, u_int ring_nr, int flags)
{
	return 0;
}

int xenvif_netmap_rxsync(struct netmap_adapter *na, u_int ring_nr, int flags)
{
	struct netmap_kring *kring = &na->rx_rings[ring_nr];
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = ring->head;
	int n;

	if (!netif_carrier_ok(na->ifp)) {
		return 0;
	}
	if (head > lim) {
		n = netmap_ring_reinit(kring);
		goto done;
	}

	rmb();
	/* First part, import newly received packets. */
	/* actually nothing to do here, they are already in the kring */

	/* Second part, skip past packets that userspace has released. */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		/* consistency check, but nothing really important here */
		for (n = 0; likely(nm_i != head); n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			void *addr = BDG_NMB(na, slot);

			if (addr == netmap_buffer_base) { /* bad buf */
				D("bad buffer index %d, ignore ?",
					slot->buf_idx);
			}
			slot->flags &= ~NS_BUF_CHANGED;
			nm_i = nm_next(nm_i, lim);
		}
		kring->nr_hwcur = head;
		wmb();
	}

	nm_rxsync_finalize(kring);
done:
	n = 0;
	return n;

}

int xenvif_notify(struct netmap_adapter *na, u_int ring_nr,
	enum txrx tx, int flags)
{
	struct ifnet *ifp = na->ifp;
	struct SOFTC_T *vif = netdev_priv(ifp);
	int is_host_ring = ring_nr == na->num_rx_rings;
	struct netmap_kring *kring;
	struct netmap_ring *ring;
	struct netmap_vpwrap_adapter *vwna = na->na_private;
	struct netmap_adapter *vna = vwna->up;

	int error;

	ND("%s %s%d 0x%x", NM_IFPNAME(ifp),
		(tx == NR_TX ? "TX" : "RX"), ring_nr, flags);

	if (ifp == NULL || !(ifp->if_capenable & IFCAP_NETMAP))
		return 0;

	if (is_host_ring) {
		return 0;
	}

	if (!netif_carrier_ok(ifp)) {
		return 0;
	}

	/* we only care about receive interrupts */
	if (tx == NR_TX)
		return 0;

	vif = netdev_priv(vna->ifp);
	if (vif == NULL)
		return 0;

	kring = &na->rx_rings[ring_nr];
	ring = kring->ring;

	/* make sure the ring is not disabled */
	if (nm_kr_tryget(kring))
		return 0;

	error = xenvif_netmap_rxsync(na, ring_nr, 0);
	if (vwna->guest_need_rxkick) {
		vwna->guest_need_rxkick = 0;
		if (vif->rx_irq)
			notify_remote_via_irq(vif->rx_irq);
	}

	nm_kr_put(kring);
	return error;
}

void xenvif_netmap_attach(struct SOFTC_T *vif)
{
	struct netmap_adapter na;

	bzero(&na, sizeof(na));
	na.ifp = vif->dev;
	BUG_ON(na.ifp->priv_flags & IFCAP_NETMAP);

	na.num_tx_desc = default_tx_desc;
	na.num_rx_desc = default_rx_desc;
	na.num_tx_rings = na.num_rx_rings = 1;
	na.nm_txsync = xenvif_netmap_rxsync;
	na.nm_rxsync = xenvif_netmap_txsync;
	na.nm_register = xenvif_netmap_reg;
	na.nm_notify = xenvif_notify;

	netmap_attach(&na);
}

void xenvif_netmap_detach(struct SOFTC_T *vif)
{
	xenvif_netmap_unmap(vif);
	netmap_detach(vif->dev);
	pr_info("netmap_detach %s", vif->dev->name);
}

static void xenvif_grant_ring(struct nm_grant_ring *gnt, struct netmap_mem_d *nm_mem,
		struct netmap_kring *kring, int num_desc, int domid)
{
	struct netmap_ring *ring = kring->ring;
	int nr_ring_pg = RING_NR_PAGES(num_desc) + 1;
	int nr_bufs_pg = BUFS_NR_PAGES(ring) + 1;
	int i, j;

	gnt->ring_pg = vmalloc(sizeof(struct page*) * nr_ring_pg);
	gnt->buf_pg = vmalloc(sizeof(struct page*) * nr_bufs_pg);
	gnt->ring_ref = vmalloc(sizeof(int) * nr_ring_pg);
	gnt->buf_ref = vmalloc(sizeof(int) * nr_bufs_pg);

	gnt->nr_ring_pgs = nm_ring_pages(kring, num_desc, gnt->ring_pg);
	gnt->nr_buf_pgs = nm_buf_pages(kring, num_desc,	nm_mem, gnt->buf_pg);

	for (i = 0, j = 0;  i < num_desc; ++i) {
		if (VIFGREF(ring,i) == 1 && gnt->buf_pg[j] != NULL) {
			gnt->buf_ref[j] = gnttab_grant_foreign_access(domid,
				pfn_to_mfn(page_to_pfn(gnt->buf_pg[j])), 0);

			VIFGREF(ring,i) = gnt->buf_ref[j];
			++j;
			wmb();
		}
	}

	for (i = 0;  i <= gnt->nr_ring_pgs; ++i) {
		gnt->ring_ref[i] = gnttab_grant_foreign_access(domid,
			pfn_to_mfn(page_to_pfn(gnt->ring_pg[i])), 0);
	}
}

static void xenvif_revoke_ring(struct nm_grant_ring *gnt)
{
	int i;
	for (i = 0;  i <= gnt->nr_ring_pgs; ++i) {
		gnttab_end_foreign_access(gnt->ring_ref[i], 0, 0);
	}

	for (i = 0;  i <= gnt->nr_buf_pgs; ++i) {
		gnttab_end_foreign_access(gnt->buf_ref[i], 0, 0);
	}

	gnt->nr_buf_pgs = 0;
	gnt->nr_ring_pgs = 0;

	vfree(gnt->ring_ref);
	vfree(gnt->buf_ref);
	vfree(gnt->ring_pg);
	vfree(gnt->buf_pg);
}

static void xenvif_netmap_map(struct SOFTC_T *info, struct netmap_adapter *na)
{
	int err, nr_ring_pg, nr_bufs_pg;
	struct xenbus_device *xbdev = to_xenbus_device(info->dev->dev.parent);
	struct xenbus_transaction xbt;
	const char *message;

	if (info->nmtx.nr_ring_pgs)
		return;

	pr_devel("grant %s rings\n", info->dev->name);

	xenvif_grant_ring(&info->nmtx, na->nm_mem, &na->tx_rings[0],
			na->num_tx_desc, info->domid);

	xenvif_grant_ring(&info->nmrx, na->nm_mem, &na->rx_rings[0],
			na->num_rx_desc, info->domid);

	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(xbdev, err, "starting transaction");
		goto abort_transaction;
	}

	xenbus_printf(xbt, xbdev->nodename, "feature-netmap-tx-desc", "%u", na->num_tx_desc);
	xenbus_printf(xbt, xbdev->nodename, "feature-netmap-rx-desc", "%u", na->num_rx_desc);

	err = xenbus_write_grefs(xbdev, &xbt, info);
	if (err) {
		message = "writing tx/rx refs";
		goto abort_transaction;
	}
abort_transaction:
	err = xenbus_transaction_end(xbt, 0);

	nr_ring_pg = info->nmtx.nr_ring_pgs + info->nmrx.nr_ring_pgs;
	nr_bufs_pg = info->nmtx.nr_buf_pgs + info->nmrx.nr_buf_pgs;
	pr_info("%s: granted %d %d (ring/buffers)\n", info->dev->name, nr_ring_pg, nr_bufs_pg);
}

static void xenvif_netmap_unmap(struct SOFTC_T *vif)
{
	int nr_pg = 0;
	struct SOFTC_T *info = vif;

	if (!info->nmtx.nr_ring_pgs)
		return;

	nr_pg = info->nmtx.nr_ring_pgs + info->nmtx.nr_buf_pgs;
	nr_pg += info->nmrx.nr_ring_pgs + info->nmrx.nr_buf_pgs;

	pr_info("%s: revoking %d pages\n", info->dev->name, nr_pg);

	xenvif_revoke_ring(&info->nmtx);
	xenvif_revoke_ring(&info->nmrx);
}

static
int xenbus_write_grefs(struct xenbus_device *dev, struct xenbus_transaction *bus,
			struct xenvif *vif)
{
	int i, err;
	struct xenbus_transaction xbt = *bus;
	struct xenvif *info = vif;
	char nr_name[ sizeof("rx-ring-refs") + 2 ];

	// Write the tx-ring-refs
   	for (i = 0; i <= info->nmtx.nr_ring_pgs; i++) {
		char name[ sizeof("tx-ring-ref") + 2 ];
		snprintf(name, sizeof(name), "tx-ring-ref%u", i);
		err = xenbus_printf(xbt, dev->otherend, name, "%u", info->nmtx.ring_ref[i]);

		if (err) return err;
	}

	snprintf(nr_name, sizeof(nr_name), "tx-ring-refs");
	err = xenbus_printf(xbt, dev->otherend, nr_name, "%u", info->nmtx.nr_ring_pgs + 1);

	// Write the rx-ring-refs
	for (i = 0; i <= info->nmrx.nr_ring_pgs; i++) {
		char name[ sizeof("rx-ring-ref") + 2 ];
		snprintf(name, sizeof(name), "rx-ring-ref%u", i);
		err = xenbus_printf(xbt, dev->otherend, name, "%u", info->nmrx.ring_ref[i]);

		if (err) return err;
	}

	snprintf(nr_name, sizeof(nr_name), "rx-ring-refs");
	err = xenbus_printf(xbt, dev->otherend, nr_name, "%u", info->nmrx.nr_ring_pgs + 1);
	return 0;
}

static
uint16_t nm_ring_pages(struct netmap_kring *kring, int ndesc,
			   struct page* pages[])
{
	struct page * page_ring_start;
	struct page * page_ring_end;
	struct page * page_idx;

	struct page ** ring_refs = pages;
	int nr_pages;

	page_ring_start = virt_to_page(kring->ring);
	page_ring_end = virt_to_page(&kring->ring->slot[ndesc - 1]);

	pr_devel("ring has %ld pages", (page_ring_end - page_ring_start));

	page_idx = page_ring_start;

	while (page_idx <= page_ring_end) {
		*ring_refs = page_idx;
		++page_idx;
		++ring_refs;
	}

	nr_pages = (page_ring_end - page_ring_start) + 1;
	return nr_pages;
}

static
uint16_t nm_buf_pages(struct netmap_kring *kring, int ndesc,
			   struct netmap_mem_d *nm_mem,
			   struct page* pages[])
{
	struct page * page_idx;
	int i = 0;
	uint32_t buf_idx;
	struct lut_entry *lut = nm_mem->pools[NETMAP_BUF_POOL].lut;
	struct page ** bufs_refs = pages;
	struct page ** bufs_start = bufs_refs;
	unsigned nr_pages;

	while (i < ndesc) {
		buf_idx = kring->ring->slot[i].buf_idx;
		page_idx = virt_to_page( lut[buf_idx].vaddr );

		VIFGREF(kring->ring,i) = 0;
		if (*bufs_refs != page_idx) {
			*(i ? ++bufs_refs : bufs_refs) = page_idx;
			VIFGREF(kring->ring,i) = 1;
		}
		++i;
	}

	nr_pages = bufs_refs - bufs_start;
	pr_devel("buffers have %d pages", nr_pages);
	return nr_pages;
}

#endif /* __XEN_NETBACK_NETMAP_H__ */
