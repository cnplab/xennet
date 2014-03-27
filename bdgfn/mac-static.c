/*
 *          bdgfn
 *
 *   file: mac-static.c
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

#define BDGFN_API		1
#define BDGFN_NAME		"stmac"
#define BDGFN_MAC_ADDR	1
#define BDGFN_IP_ADDR	0

#include "bdgfn_kern.h"

int rt_ctl = 0;
module_param(rt_ctl, uint, 0755);
MODULE_PARM_DESC(rt_ctl, "Control port in the switch");

struct nm_route_ent {
	uint64_t	mac;
	uint64_t	ports;
};

struct nm_route {
	struct nm_route_ent ht[NM_BDG_HASH];
	uint64_t ctl;
};

static struct nm_route rt[NM_BRIDGES];

u_int lookup2(char *buf, u_int len, uint8_t *dst_ring,
		struct netmap_vp_adapter *na)
{
	struct nm_route_ent *ht = rt[0].ht;
	u_int dst = rt[0].ctl;
	uint32_t sh;

	if ((buf[6] & 1) == 0) {
		sh = nm_bridge_rthash(buf+6);		
		dst = ht[sh].ports;
	}

	*dst_ring = 0;
	return dst;
}

int bdgaddif2(char *s, u_int bdg_idx, u_int dst)
{
	struct nm_route_ent *ht = rt[bdg_idx].ht;
	uint32_t sh = nm_bridge_rthash(s);
	uint64_t smac;
	smac = le64toh(*(uint64_t *)(s)) & 0xffffffffffff;
	ht[sh].mac = (uint64_t) smac;
	ht[sh].ports = dst;
    pr_warn("src %02x:%02x:%02x:%02x:%02x:%02x -> %d [addr %ld sh %d]\n",
			s[0], s[1], s[2], s[3], s[4], s[5], dst, ht[sh].mac, sh);
	return 0;
}

int bdglist(u_int xiter, struct bdgreq *breq)
{
	struct nm_route_ent *ht = rt[breq->bdg_idx].ht;
	int rc = ENOENT, i = xiter;

	uint64_t addr;
	uint8_t *s;

	for (; i < NM_BDG_HASH; ++i) {
		if (!ht[i].mac) {
			continue;
		}

    	addr = htole64(ht[i].mac);
		s = (uint8_t *) &addr;
		breq->addr[0] = s[0];
		breq->addr[1] = s[1];
		breq->addr[2] = s[2];
		breq->addr[3] = s[3];
		breq->addr[4] = s[4];
		breq->addr[5] = s[5];
		breq->bdg_port = ht[i].ports;
		breq->nr_arg1 = i;
		rc = 0;
		break;		
	}
	return rc;
}

long bdgctl(struct file *file, u_int cmd, u_long data)
{
	int ret = 0;
	struct bdgreq *breq = (struct bdgreq *) data;

	switch(cmd) {

	case BDGIOCREG:
		ret = bdgfnreg(breq->bdg_idx, (bdg_lookup_fn_t) lookup2);
		rt[breq->bdg_idx].ctl = breq->bdg_port;
		pr_warn("vale%d using static addr lookup\n", breq->bdg_idx);
		break;

	case BDGIOCLIST:
		ret = bdglist(breq->nr_arg1, breq);
		break;

	case BDGIOCADDIF:
		ret = bdgaddif2(breq->addr, breq->bdg_idx, breq->nr_dst);
		break;
	}

	return ret;
}

EXPORT_SYMBOL(lookup2);
EXPORT_SYMBOL(bdgaddif2);

DEFINE_BDGFN("macs");

static void rt_init(struct nm_route *r)
{
	int j;
	
	r->ctl = NM_BDG_NOPORT;
	for (j=0; j < NM_BDG_HASH; ++j) {
		r->ht[j].ports = NM_BDG_NOPORT;
	}
}

static int __init mac_static_init(void)
{
	int error, i;
	
	bzero(rt, sizeof(struct nm_route) * NM_BRIDGES); /* safety */
	for (i=0; i < NM_BRIDGES; ++i) 
		rt_init(&rt[i]);
	
	bdgfndev = make_dev(&bdg_cdevsw, 0, UID_ROOT, GID_WHEEL, 0660,
			      BDGFN_NAME);
	return 0;
}

static void __exit mac_static_exit(void)
{
	destroy_dev(bdgfndev);

	// revert to the default netmap learning bridge
	bdgfnreg(0, bdg_learning);
}

module_init(mac_static_init);
module_exit(mac_static_exit);

MODULE_AUTHOR("Joao Martins (joao.martins@neclab.eu)");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("L2 static bridge example");
MODULE_VERSION("0.1.0");
