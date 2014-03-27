/*
 *          bdgfn
 *
 *   file: ip-static.c
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
#define BDGFN_NAME		"stip"

#include "bdgfn_kern.h"

#include <linux/if_arp.h>

#define ETHER_HDR_LEN	ETH_HLEN
struct ip {
#if defined(__LITTLE_ENDIAN_BITFIELD)
        u_char  ip_hl:4,                /* header length */
                ip_v:4;                 /* version */
#elif defined (__BIG_ENDIAN_BITFIELD)
        u_char  ip_v:4,                 /* version */
                ip_hl:4;                /* header length */
#endif
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
#define IP_RF 0x8000                    /* reserved fragment flag */
#define IP_DF 0x4000                    /* dont fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
} __packed __aligned(4);

struct ip6_hdr {
        union {
                struct ip6_hdrctl {
                        u_int32_t ip6_un1_flow; /* 20 bits of flow-ID */
                        u_int16_t ip6_un1_plen; /* payload length */
                        u_int8_t  ip6_un1_nxt;  /* next header */
                        u_int8_t  ip6_un1_hlim; /* hop limit */
                } ip6_un1;
                u_int8_t ip6_un2_vfc;   /* 4 bits version, top 4 bits class */
        } ip6_ctlun;
        struct in6_addr ip6_src;        /* source address */
        struct in6_addr ip6_dst;        /* destination address */
} __packed;

#define ip6_vfc         ip6_ctlun.ip6_un2_vfc
#define ip6_flow        ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen        ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt         ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim        ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops        ip6_ctlun.ip6_un1.ip6_un1_hlim

#define	ETHER_ADDR_LEN		6
struct	ether_header {
	u_char	ether_dhost[ETHER_ADDR_LEN];
	u_char	ether_shost[ETHER_ADDR_LEN];
	u_short	ether_type;
};
#define	ETHERTYPE_IP		0x0800	/* IP protocol */
#define ETHERTYPE_IPV6		0x86dd	/* IPv6 */
#define ETHERTYPE_ARP		0x0806	/* Addr. resolution protocol */

int rt_ctl = 0;
module_param(rt_ctl, uint, 0755);
MODULE_PARM_DESC(rt_ctl, "Control port in the switch");

struct nm_route_ent {
	uint64_t	addr;
	uint64_t	ports;
};

#undef NM_BDG_HASH
#undef NM_BRIDGES

#define IP_BDG_HASH 		65536
#define NM_BDG_HASH			IP_BDG_HASH
#define NM_BRIDGES			4

struct nm_route {
	struct nm_route_ent ht[NM_BDG_HASH];
	uint64_t ctl;
};

static struct nm_route rt[NM_BRIDGES];

static __inline uint32_t
iphash2(const uint8_t *addr)
{
	uint32_t c = (addr[3] << 8) | addr[2];
#define BRIDGE_RTHASH_MASK	(NM_BDG_HASH-1)
	return (c & BRIDGE_RTHASH_MASK);
}


static __inline uint32_t
iphash(const uint8_t *addr)
{
	uint32_t c = 0; // hask key
	c = addr[3] + addr[2] + addr[1] + addr[0];
	return (c & BRIDGE_RTHASH_MASK);
}

#define MAC_LOOKUP_FIRST 1
#ifdef MAC_LOOKUP_FIRST
/* MAC static lookup */
u_int lookup2(char *buf, u_int len, uint8_t *dst_ring,
		struct netmap_vp_adapter *na);
#endif

/* Search for MAC and afterwards */
u_int lookup3(char *buf, u_int len, uint8_t *dst_ring,
		struct netmap_vp_adapter *na)
{

	struct nm_route_ent *ht = rt[0].ht;
	u_int ctl = rt[0].ctl;
#ifndef MAC_LOOKUP_FIRST
	u_int dst = ctl;
#else
	u_int dst = lookup2(buf,len,dst_ring,na);
#endif
	u_int ether_type;
	uint32_t sh, dh;
	uint32_t *saddr = NULL, *daddr = NULL;
	char *s, *d;

	if (dst != NM_BDG_NOPORT) {
		goto out;
	}
	
	ether_type = ntohs(*((uint16_t *)(buf + ETHER_ADDR_LEN * 2)));
	if (ether_type == ETHERTYPE_IP) {
		struct ip *iph = (struct ip *)(buf + ETHER_HDR_LEN);
		saddr = (uint32_t *)&iph->ip_src;
		daddr = (uint32_t *)&iph->ip_dst;
	} else if (ether_type == ETHERTYPE_IPV6) {
		struct ip6_hdr *ip6 = (struct ip6_hdr *)(buf + ETHER_HDR_LEN);
		saddr = (uint32_t *)&ip6->ip6_src;
		daddr = (uint32_t *)&ip6->ip6_dst;
	}

	if (saddr) {
		sh = iphash2((uint8_t *) saddr);
		dst = ht[sh].ports;
	}

#ifndef IP_LOOKUP_PERFLOW
	if (dst == ctl && daddr) {
		dh = iphash2((uint8_t *) daddr);
		dst = ht[dh].ports;
	}
#endif
#if 0
	s = (char *) saddr;	d = (char *) daddr;
	D("%u.%u.%u.%u -> %u.%u.%u.%u [%u, %u]", s[0], s[1], s[2], s[3], 
					d[0], d[1], d[2], d[3], sh, dh);
#endif
out:
	*dst_ring = 0;
	return dst;
}

int bdgaddif3(char *s, u_int bdg_idx, u_int dst)
{
	struct nm_route_ent *ht = rt[bdg_idx].ht;
	uint32_t sh = iphash2(s);
	uint32_t ip_src;
	ip_src = (*(uint32_t *) (s)) & 0xffffffff;
	
	ht[sh].addr = ip_src;
	ht[sh].ports = dst;
    
	pr_warn("src %u.%u.%u.%u -> %u [addr %lu sh %u]\n",
			s[0], s[1], s[2], s[3], dst, ht[sh].addr, sh);
	return 0;
}

int bdglist(u_int xiter, struct bdgreq * breq)
{
	struct nm_route_ent *ht = rt[breq->bdg_idx].ht;
	int rc = ENOENT, i = xiter;

	uint64_t addr;
	uint8_t *s;

	for (; i < NM_BDG_HASH; ++i) {
		if (!ht[i].addr) {
			continue;
		}

    	addr = ht[i].addr;
		s = (uint8_t *) &addr;
		breq->addr[0] = s[0];
		breq->addr[1] = s[1];
		breq->addr[2] = s[2];
		breq->addr[3] = s[3];
		breq->bdg_port = ht[i].ports;
		breq->nr_arg1 = i;
		rc = 0;
		break;		
	}
	return rc;
}

static void rt_init(struct nm_route *r, u_int ctl)
{
	int j;
	
	r->ctl = NM_BDG_NOPORT;
	for (j=0; j < NM_BDG_HASH; ++j) {
		r->ht[j].ports = ctl;
	}
}

long bdgctl(struct file *file, u_int cmd, u_long data)
{
	int ret = 0;
	struct bdgreq *breq = (struct bdgreq *) data;
	bdg_lookup_fn_t func = lookup3;

	switch(cmd) {

	case BDGIOCREG:
		ret = bdgfnreg(breq->bdg_idx, func);		
		rt_init(&rt[breq->bdg_idx], breq->bdg_port);
		rt[breq->bdg_idx].ctl = breq->bdg_port;
		pr_warn("vale%d:%d using static ip4/6 lookup\n", breq->bdg_idx, breq->bdg_port);
		break;

	case BDGIOCLIST:
		ret = bdglist(breq->nr_arg1, breq);
		break;

	case BDGIOCADDIF:
		ret = bdgaddif3(breq->addr, breq->bdg_idx, breq->nr_dst);
		break;
	}

	return ret;
}

EXPORT_SYMBOL(lookup3);
EXPORT_SYMBOL(bdgaddif3);
DEFINE_BDGFN("ips");


static int __init ip_static_init(void)
{
	int error;

	bzero(rt, sizeof(struct nm_route) * NM_BRIDGES); /* safety */
	bdgfndev = make_dev(&bdg_cdevsw, 0, UID_ROOT, GID_WHEEL, 0660,
			      BDGFN_NAME);
	return 0;
}

static void __exit ip_static_exit(void)
{
	destroy_dev(bdgfndev);

	// revert to the default netmap learning bridge
	bdgfnreg(0, bdg_learning);
}

module_init(ip_static_init);
module_exit(ip_static_exit);

MODULE_AUTHOR("Joao Martins (joao.martins@neclab.eu)");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("L3 static example");
MODULE_VERSION("0.1.0");
