/*
 *          bdgfn
 *
 *   file: learn.c
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
#include "bdgfn_kern.h"

enum {
	MAC = 0,
	MACLSB = 1,
};

int type = 0;
int nodst = NM_BDG_BROADCAST;

module_param(type, uint, 0755);
MODULE_PARM_DESC(type, "Bridge type");

u_int lsb_learning(char *buf, u_int len, uint8_t *dst_ring,
		struct netmap_adapter *na)
{
	struct nm_hash_ent *ht = na->na_bdg->ht;
	uint32_t sh, dh;
	u_int dst, mysrc = na->bdg_port;

	if ((buf[6] & 1) == 0) { /* valid src */
		uint8_t *s = buf+6;
	    uint64_t smac;
		sh = (uint32_t) s[5];
	    smac = le64toh(*(uint64_t *)(buf + 4));
    	smac >>= 16;

		ht[sh].mac = smac;
		ht[sh].ports = mysrc;
	}

	dst = nodst;

	if ((buf[0] & 1) == 0) { /* unicast */
		uint8_t *d = buf;
	    uint64_t dmac;
		dh = (uint32_t) d[5];
	    dmac = le64toh(*(uint64_t *)(buf)) & 0xffffffffffff;
		if (ht[dh].mac == dmac) {	/* found dst */
			dst = ht[dh].ports;
        }
	}
	*dst_ring = 0;
	return dst;
}

static int __init mac_lsb_init(void)
{
	bdg_lookup_fn_t func = bdg_learning;

	if (type) {
		func = lsb_learning;
	}

	pr_warn("using %s L2 learning bridge\n", 
				func == bdg_learning ? "default" : "lsb");

	bdgfnreg(0, func);
	return 0;
}

static void __exit mac_lsb_exit(void)
{
	// revert to the default netmap learning bridge
	bdgfnreg(0, bdg_learning);
}

module_init(mac_lsb_init);
module_exit(mac_lsb_exit);

MODULE_AUTHOR("Joao Martins (joao.martins@neclab.eu)");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("L2 learning bridge example");
MODULE_VERSION("0.1.0");
