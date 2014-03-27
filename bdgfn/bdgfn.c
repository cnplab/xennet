/*
 *          bdgfn
 *
 *   file: bdgfn.c
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
#include "bdgfn.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#define IFNAMSIZ	16
#include <string.h>	/* strcmp */
#include <net/netmap.h>
#include <net/netmap_user.h>
#undef IFNAMSIZ

/* debug support */
#define ND(format, ...)	do {} while(0)
#define D(format, ...)					\
	fprintf(stderr, "%s: %s [%d] " format "\n",		\
		bdgfn, __FUNCTION__, __LINE__, ##__VA_ARGS__)

char *bdgfn = NULL;
int bdgenv = 0;
static int bdgfd = -1;

int bdgfninit()
{		
	bdgfd = open(bdgfn, O_RDWR);
	if (bdgfd < 0) {
		perror("bdgfn:");
		return 1;
	}
}

int bdgfnfini()
{
	close(bdgfd);
}

static void pr_route(char *m, int bdg_idx, int bdg_port, int nr_dst)
{
	if (bdgenv & BDGCTL_ENV_PORT)
		fprintf(stderr, "[+] vale%d:%d -> %d\n", 
			bdg_idx, bdg_port, nr_dst);
	if (bdgenv & BDGCTL_ENV_MAC)
		fprintf(stderr, "[+] %02d:%02d:%02d:%02d:%02d:%02d -> %d\n", 
			m[0], m[1], m[2], m[3], m[4], m[5], nr_dst);
	if (bdgenv & BDGCTL_ENV_IP4)
		fprintf(stderr, "[+] %d.%d.%d.%d %8s %d\n",
			m[0], m[1], m[2], m[3], "->", nr_dst);
}

static int bdgfn_do(u_int cmd, struct bdgreq *breq)
{
	int rc = 0;
	switch (cmd)
	{
		case BDGIOCREG:
			rc = ioctl(bdgfd, BDGIOCREG, breq);
			if (rc) 
				D("cannot register vale%d", breq->bdg_idx);
			else
				D("register vale%d bdg_port %d", breq->bdg_idx, breq->bdg_port);
			break;
		case BDGIOCADDIF:
			{
			int rc = 0;
			pr_route(breq->addr, breq->bdg_idx, breq->bdg_port, breq->nr_dst);
			rc = ioctl(bdgfd, BDGIOCADDIF, breq);
			}
			break;
		case BDGIOCLIST:
			{
			breq->nr_arg1 = 0;
			for (; !ioctl(bdgfd, BDGIOCLIST, breq); breq->nr_arg1++) {
				char *s = breq->addr;
				if (bdgenv & BDGCTL_ENV_MAC)
					fprintf(stderr, "[%04d] %02d:%02d:%02d:%02d:%02d:%02d -> %04d\n", 
						breq->nr_arg1, s[0], s[1], s[2], s[3], s[4], s[5], 
						breq->bdg_port);
				if (bdgenv & BDGCTL_ENV_IP4)
					fprintf(stderr, "[%04d] %d.%d.%d.%d %8s %04d\n", breq->nr_arg1, 
						s[0], s[1], s[2], s[3], "->", breq->bdg_port);
			}
			}
			break;
		default:
			D("unknown command");
	}

	return rc;
}

int bdgfnreg(struct bdgreq *breq)
{
	return bdgfn_do(BDGIOCREG, breq);
}

int bdglistif(struct bdgreq *breq)
{
	return bdgfn_do(BDGIOCLIST, breq);
}

int bdgaddif(struct bdgreq *breq)
{
	return bdgfn_do(BDGIOCADDIF, breq);
}

int bdgdelif(struct bdgreq *breq)
{
	return -ENOENT;
}

/* Taken form vale-ctl.c in netmap examples*/
int bdgport(int bdg_idx, char *name)
{
	struct nmreq nmr;
	int error = 0;
	int fd = open("/dev/netmap", O_RDWR);

	if (fd == -1) {
		D("Unable to open /dev/netmap");
		return -1;
	}

	bzero(&nmr, sizeof(nmr));
	nmr.nr_version = NETMAP_API;
	if (name != NULL) /* might be NULL */
		strncpy(nmr.nr_name, name, sizeof(nmr.nr_name));
	nmr.nr_cmd = NETMAP_BDG_LIST;

	if (strlen(nmr.nr_name)) { /* name to bridge/port info */
		error = ioctl(fd, NIOCGINFO, &nmr);
		//if (error)
			//D("Unable to obtain info for %s", name);
		//nmr.nr_arg2 = error;
	}

	return nmr.nr_arg2;
}
