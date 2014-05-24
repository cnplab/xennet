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
xnb_sendbuf(netif_rx_back_ring_t *ring, void *buf, u_int len,
	u_int gnt_idx, gnttab_copy_table gnttab, domid_t otherend_id)
{
	const netif_rx_request_t *rxq = RING_GET_REQUEST(ring, ring->rsp_prod_pvt);
	int ofs = virt_to_offset( (vm_offset_t) buf);
	
	gnttab[gnt_idx].dest.u.ref = rxq->gref;
	gnttab[gnt_idx].dest.domid = otherend_id;
	gnttab[gnt_idx].dest.offset = ofs;
	gnttab[gnt_idx].source.u.gmfn = virt_to_mfn( (vm_offset_t) buf);
	gnttab[gnt_idx].source.offset = ofs;
	gnttab[gnt_idx].source.domid = DOMID_SELF;
	gnttab[gnt_idx].len = len;
	gnttab[gnt_idx].flags = GNTCOPY_dest_gref;
	
	NDPRINTF("pkt address= %p\n", buf);
	NDPRINTF("pkt:gnttab gnt_idx= %u\n", gnt_idx);
	NDPRINTF("pkt:gnttab size= %u\n", gnttab[gnt_idx].len);
	NDPRINTF("pkt:gnttab source.u.gmfn= %lu\n", gnttab[gnt_idx].source.u.gmfn);
	NDPRINTF("pkt:gnttab source.offset= %u\n", gnttab[gnt_idx].source.offset);
	
	return 0;
}

static int
xnb_gntmap(netif_rx_back_ring_t *ring, gnttab_map_table gntmap, int n_entries)
{
	return 0;
}

static int
xnb_gntcopy(netif_rx_back_ring_t *ring, gnttab_copy_table gnttab, int n_entries)
{
	int error = 0;
	int gnt_idx, i;
	int n_responses = 0;
	RING_IDX r_idx;

	int __unused hv_ret = HYPERVISOR_grant_table_op(GNTTABOP_copy,
		gnttab, n_entries);
	
	for (gnt_idx = 0; gnt_idx < n_entries; gnt_idx++) {
		int16_t status = gnttab[gnt_idx].status;
		if (status != GNTST_okay) {
			DPRINTF(
			    "Got error %d (gnt_idx=%u n_entries=%d) for hypervisor gnttab_copy status\n",
			    status, gnt_idx, n_entries);
			error = 1;
			break;
		}
		n_responses++;
	}
	
	if (error) {
		uint16_t id;
		netif_rx_response_t *rsp;
		
		id = RING_GET_REQUEST(ring, ring->rsp_prod_pvt)->id;
		rsp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
		rsp->id = id;
		rsp->status = NETIF_RSP_ERROR;
		n_responses = 1;
	} else {
		gnt_idx = 0;
		for (i = 0; i < n_responses; i++) {
			netif_rx_request_t rxq;
			netif_rx_response_t *rsp;

			r_idx = ring->rsp_prod_pvt + i;
			/*
			 * We copy the structure of rxq instead of making a
			 * pointer because it shares the same memory as rsp.
			 */
			rxq = *(RING_GET_REQUEST(ring, r_idx));
			rsp = RING_GET_RESPONSE(ring, r_idx);
			rsp->id = rxq.id;
			rsp->flags = 0;
			rsp->offset = gnttab[gnt_idx].dest.offset;
			rsp->status = gnttab[gnt_idx].len;
			gnt_idx++;
		}
	}

	return n_responses;
}

static int
xnb_recvbuf(netif_tx_back_ring_t *ring, void *pkt, u_int len,
	gnttab_copy_table gnttab)
{
	return 0;
}

static int
xnb_netmap_txsync(struct netmap_adapter *na, u_int ring_nr, int flags)
{
	struct netmap_kring *kring = &na->tx_rings[ring_nr];
	struct netmap_ring *ring = kring->ring;
	u_int n;
	u_int const cur = kring->rcur;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int done;

	/* device-specific */
	struct xnb_softc *xnb = na->ifp->if_softc;
	netif_rx_back_ring_t *rxr = &xnb->ring_configs[XNB_RING_TYPE_RX].back_ring.rx_ring;
	RING_IDX req_prod_local = rxr->sring->req_prod;
	RING_IDX space;
	int n_responses, notify;

	done = kring->nr_hwcur;
	space = req_prod_local - rxr->req_cons;

	if (!space)
		goto txsync_done;

	xen_rmb();
	
	for (n = 0; done != cur; n++) {
		struct netmap_slot *slot = &ring->slot[done];
		u_int len = slot->len;
		uint64_t paddr;
		void *addr = PNMB(slot, &paddr);
		
		if (n == space)
			break;

		/* device-specific */
		xnb_sendbuf(rxr, addr, len, n, xnb->tx_gnttab, 
			xnb->otherend_id);

		done = nm_next(done, lim);
	}
	
	NDPRINTF("pkt:ring: space= %d req_cons= %d req_prod= %d\n\tn= %d\n", 
		space, rxr->req_cons, req_prod_local, n);

	NDPRINTF("pkt:gntcopy: base= %p gops= %d\n", xnb->tx_gnttab, n);
	n_responses = xnb_gntcopy(rxr, xnb->tx_gnttab, n);
	rxr->req_cons += n_responses;
	rxr->rsp_prod_pvt += n_responses;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(rxr, notify);
	
	if (notify != 0)
		xen_intr_signal(xnb->xen_intr_handle);
	
	rxr->sring->req_event = req_prod_local + 1;
	xen_mb();
	
	kring->nr_hwcur = done;
	kring->nr_hwtail = nm_prev(done, lim);
txsync_done:
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
			xnb_recvbuf(rxr, addr, len, xnb->rx_gnttab);

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
	
	bzero(&na, sizeof(na));

	na.ifp = xnb->xnb_ifp;
	na.na_flags = NAF_BDG_MAYSLEEP;
	na.num_tx_desc = GNTTAB_LEN;
	na.num_rx_desc = GNTTAB_LEN;
	na.nm_txsync = xnb_netmap_txsync;
	na.nm_rxsync = xnb_netmap_rxsync;
	na.nm_register = xnb_netmap_reg;

	na.num_tx_rings = na.num_rx_rings = 1; // XXX feature-multi-queue
	DPRINTF("%s: txd %d rxd %d \n", xnb->xnb_ifp->if_xname, na.num_tx_desc, na.num_rx_desc);

	netmap_attach(&na);
}
