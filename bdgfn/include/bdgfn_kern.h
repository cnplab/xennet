/*
 *          bdgfn
 *
 *   file: bdgfn_kern.h
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
#include <bsd_glue.h>
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>

#define IFNAMSIZ			16  	/*  */
#define	NM_BRIDGES			8		/* number of bridges */
#define NM_BDG_HASH			1024	/* forwarding table entries */
#define	NM_BDG_BROADCAST	NM_BDG_MAXPORTS
#define	NM_BDG_NOPORT		(NM_BDG_MAXPORTS+1)
#define	NM_ROUTE_LOOKUP		(NM_BDG_MAXPORTS+2)

struct nm_hash_ent {
	uint64_t	mac;	/* the top 2 bytes are the epoch */
	uint64_t	ports;
};

#if NETMAP_API < 5
struct nm_bridge {
	int namelen;	/* 0 means free */
#if NETMAP_API > 3
	BDG_RWLOCK_T bdg_lock;	/* protects bdg_ports */
#else
	NM_RWLOCK_T bdg_lock;	/* protects bdg_ports */
#endif
	struct netmap_adapter *bdg_ports[NM_BDG_MAXPORTS];
	char basename[IFNAMSIZ];
	bdg_lookup_fn_t nm_bdg_lookup;
	struct nm_hash_ent ht[NM_BDG_HASH];
};
#else
struct nm_bridge {
	BDG_RWLOCK_T	bdg_lock;	/* protects bdg_ports */
	int		bdg_namelen;
	uint32_t	bdg_active_ports; /* 0 means free */
	char		bdg_basename[IFNAMSIZ];
	uint8_t		bdg_port_index[NM_BDG_MAXPORTS];
	struct netmap_vp_adapter *bdg_ports[NM_BDG_MAXPORTS];
	bdg_lookup_fn_t nm_bdg_lookup;
	struct nm_hash_ent ht[NM_BDG_HASH];
};
#endif

/*
 * From sys/dev/netmap/netmap.c in FreeBSD HEAD
 * @author Luigi Rizzo
 *
 * The following hash function is adapted from "Hash Functions" by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 *
 * http://www.burtleburtle.net/bob/hash/spooky.html
 */
#define mix(a, b, c)                                                    \
do {                                                                    \
        a -= b; a -= c; a ^= (c >> 13);                                 \
        b -= c; b -= a; b ^= (a << 8);                                  \
        c -= a; c -= b; c ^= (b >> 13);                                 \
        a -= b; a -= c; a ^= (c >> 12);                                 \
        b -= c; b -= a; b ^= (a << 16);                                 \
        c -= a; c -= b; c ^= (b >> 5);                                  \
        a -= b; a -= c; a ^= (c >> 3);                                  \
        b -= c; b -= a; b ^= (a << 10);                                 \
        c -= a; c -= b; c ^= (b >> 15);                                 \
} while (/*CONSTCOND*/0)

static __inline uint32_t
nm_bridge_rthash(const uint8_t *addr)
{
        uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0; // hask key

        b += addr[5] << 8;
        b += addr[4];
        a += addr[3] << 24;
        a += addr[2] << 16;
        a += addr[1] << 8;
        a += addr[0];

        mix(a, b, c);
#define BRIDGE_RTHASH_MASK	(NM_BDG_HASH-1)
        return (c & BRIDGE_RTHASH_MASK);
}

#undef mix
u_int bdg_learning(char *buf, u_int len, uint8_t *dst_ring,
		struct netmap_vp_adapter *na)
{
	struct nm_hash_ent *ht = na->na_bdg->ht;
	uint32_t sh, dh;
	u_int dst, mysrc = na->bdg_port;
	uint64_t smac, dmac;

	dmac = le64toh(*(uint64_t *)(buf)) & 0xffffffffffff;
	smac = le64toh(*(uint64_t *)(buf + 4));
	smac >>= 16;

	if ((buf[6] & 1) == 0) { /* valid src */
		sh = nm_bridge_rthash(buf+6);
		ht[sh].mac = smac;
		ht[sh].ports = mysrc;
	}

	dst = NM_BDG_BROADCAST;

	if ((buf[0] & 1) == 0) { /* unicast */
		dh = nm_bridge_rthash(buf);
		if (ht[dh].mac == dmac) {	/* found dst */
			dst = ht[dh].ports;
		}
	}
	*dst_ring = 0;
	return dst;
}


static inline
int bdgfnreg(u_int bdg_idx, bdg_lookup_fn_t func)
{ 
	char nmstr[IFNAMSIZ];
    int err;   
	struct nmreq *nmr = NULL;
    
	/* register bridge lookup function */
    nmr = malloc(sizeof(*nmr), M_DEVBUF, M_NOWAIT | M_ZERO);
	bzero(nmr, sizeof(*nmr));
	nmr->nr_ringid = 0;
    nmr->nr_cmd = NETMAP_BDG_LOOKUP_REG;
    nmr->nr_version = NETMAP_API;

    snprintf(nmstr, IFNAMSIZ, "vale%u:", bdg_idx);
    strncpy(nmr->nr_name, nmstr, sizeof(nmr->nr_name));
	pr_warn("register `lookup_fn` %s", nmr->nr_name);
    if ((err = netmap_bdg_ctl(nmr, func)) > 0)
        return -EINVAL;
    
	return 0;
}

#if defined(BDGFN_API) && BDGFN_API
#include "bdgfn_user.h"

extern long bdgctl(struct file *file, u_int cmd, u_long data);

#define DEFINE_BDGFN(name) \
	static struct file_operations bdg_fops = { \
	    .owner = THIS_MODULE, \
	    .unlocked_ioctl = bdgctl \
	}; \
	static struct miscdevice bdg_cdevsw = {	\
		MISC_DYNAMIC_MINOR, \
		name, \
		&bdg_fops, \
	}; \
	static struct cdev *bdgfndev;
#endif
