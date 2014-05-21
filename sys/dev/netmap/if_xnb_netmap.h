/*
 *          xen-netback netmap support
 *
 *   file: if_xnb_netmap.h
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

#include <net/netmap.h>
#include <sys/selinfo.h>
#include <sys/bus_dma.h>
#include <dev/netmap/netmap_kern.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_intr.h>
#include <xen/interface/io/netif.h>

static int
xnb_netmap_reg(struct netmap_adapter *na, int onoff)
{
	struct ifnet *ifp = na->ifp;
	struct xnb_softc *xnb = ifp->if_softc;
	
	mtx_lock(&xnb->sc_lock);

	/* Tell the stack that the interface is no longer active */
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	
	/* enable or disable flags and callbacks in na and ifp */
	if (onoff) {
		nm_set_native_flags(na);
	} else {
		nm_clear_native_flags(na);
	}
	
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	
	mtx_unlock(&xnb->sc_lock);

	return (ifp->if_drv_flags & IFF_DRV_RUNNING ? 0 : 1);
}

static int 
xnb_sendbuf(netif_rx_back_ring_t *ring, void *pkt, u_int len,
	gnttab_copy_table gnttab)
{
	return 0;
}

static int
xnb_recvbuf(netif_tx_back_ring_t *ring, void *pkt, u_int len)
{
	return 0;
}

static int
xnb_netmap_txsync(struct netmap_adapter *na, u_int ring_nr, int flags)
{
	struct netmap_kring *kring = &na->tx_rings[ring_nr];
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */
	u_int nic_i;	/* index into the NIC ring */	
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;

	/* device-specific */
	struct xnb_softc *xnb = na->ifp->if_softc;
	netif_rx_back_ring_t *txr = &xnb->ring_configs[XNB_RING_TYPE_RX].back_ring.rx_ring;
	int reclaim_tx;

	nm_i = kring->nr_hwcur;
	if (nm_i != head) {	/* we have new packets to send */
		nic_i = netmap_idx_k2n(kring, nm_i);
		
		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			u_int len = slot->len;
			uint64_t paddr;
			void *addr = PNMB(slot, &paddr);

			/* device-specific */
			xnb_sendbuf(txr, addr, len, NULL);

			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		kring->nr_hwcur = head;
	}

	if (flags & NAF_FORCE_RECLAIM) {
		reclaim_tx = 1; /* forced reclaim */
	} else if (!nm_kr_txempty(kring)) {
		reclaim_tx = 0; /* have buffers, no reclaim */
	} else {
		reclaim_tx = 1; /* have buffers, no reclaim */
	}

	if (reclaim_tx) {
		// Check CSB
		kring->nr_hwtail = nm_prev(netmap_idx_n2k(kring, nic_i), lim);		
	}

	nm_txsync_finalize(kring);

	return 0;
}

static int
xnb_netmap_rxsync(struct netmap_adapter *na, u_int ring_nr, int flags)
{
	struct netmap_kring *kring = &na->rx_rings[ring_nr];
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */
	u_int nic_i;	/* index into the NIC ring */
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = nm_rxsync_prologue(kring);
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;

	/* device-specific */
	struct xnb_softc *xnb = na->ifp->if_softc;
	netif_tx_back_ring_t * rxr =
		&xnb->ring_configs[XNB_RING_TYPE_TX].back_ring.tx_ring;

	if (head > lim)
		return netmap_ring_reinit(kring);

	if (netmap_no_pendintr || force_update) {
		nic_i = kring->nr_hwtail;
		nm_i = netmap_idx_n2k(kring, nic_i);

		for (n = 0; ; n++) {
			break; // Check CSB
			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		if (n) { /* update the state variables */
			kring->nr_hwtail = nm_i;
		}
		kring->nr_kflags &= ~NKR_PENDINTR;
	}

	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		nic_i = netmap_idx_k2n(kring, nm_i);
		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			uint64_t paddr;
			u_int len = slot->len;
			void *addr = PNMB(slot, &paddr);

			if (addr == netmap_buffer_base) /* bad buf */
				goto ring_reset;
			
			/* device-specific */
			xnb_recvbuf(rxr, addr, len);

			slot->flags &= ~NS_BUF_CHANGED;
			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		kring->nr_hwcur = head;

		nic_i = nm_prev(nic_i, lim);
	}

	/* tell userspace that there might be new packets */
	nm_rxsync_finalize(kring);

	return 0;

ring_reset:
	return netmap_ring_reinit(kring);
}

static void
xnb_netmap_attach(struct xnb_softc *xnb)
{
	struct netmap_adapter na;
	struct xnb_ring_config *rxb =
		&xnb->ring_configs[XNB_RING_TYPE_RX];
	struct xnb_ring_config *txb =
		&xnb->ring_configs[XNB_RING_TYPE_TX];

	bzero(&na, sizeof(na));

	na.ifp = xnb->xnb_ifp;
	na.na_flags = NAF_BDG_MAYSLEEP;
	na.num_tx_desc = __RING_SIZE( (netif_tx_sring_t*)txb->va,
		txb->ring_pages * PAGE_SIZE);
	na.num_rx_desc = __RING_SIZE( (netif_rx_sring_t*)rxb->va,
		rxb->ring_pages * PAGE_SIZE);
	na.nm_txsync = xnb_netmap_txsync;
	na.nm_rxsync = xnb_netmap_rxsync;
	na.nm_register = xnb_netmap_reg;

	na.num_tx_rings = na.num_rx_rings = 1; // XXX feature-multi-queue
	DPRINTF("%s: txd %d rxd %d \n", xnb->xnb_ifp->if_xname, na.num_tx_desc, na.num_rx_desc);

	netmap_attach(&na);
}
