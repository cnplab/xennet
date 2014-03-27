/*
 *          bdgfn
 *
 *   file: bdgfnctl.c
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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

struct cmd_struct {
    const char *cmd;
    int (*fn)(struct bdgreq *breq);
};

int bdgfnctlport();

static struct cmd_struct commands[] = {
    { "addif", bdgaddif },
    { "delif", bdgdelif },
    { "listif", bdglistif },
    { "regfn", bdgfnreg },
    { "port", bdgfnctlport },
};

static char *vport = NULL;
int bdgfnctlport(struct bdgreq *breq)
{
	printf("port = %d\n", bdgport(breq->bdg_idx, vport));
	return 0;
}

void* parse_options(int argc, char **argv)
{
	struct option long_options[] = {
		{ "bridge", required_argument, 0, 'b'},
		{ "port", 	required_argument, 0, 'p'},
		{ "dest", 	required_argument, 0, 'd'},
		{ "mac",  	required_argument, 0, 'm'},
		{ "ip4",  	required_argument, 0, '4'},
		{ "ip6",  	required_argument, 0, '6'},
		{ "vport",  	required_argument, 0, 'V'},
    	{ 0, 0, 0, 0}
	};
	int ch, optidx = 0;
	struct bdgreq *breq;
	breq = (struct bdgreq *) malloc(sizeof(struct bdgreq));

	while ((ch = getopt_long(argc, argv, "b:p:d:m:4:6:V:", 
									long_options, &optidx)) != -1) {
		switch (ch) {

		default:
			fprintf(stderr, "bad option %c %s", ch, optarg);
			break;
		case 'b': // bdg_idx
			breq->bdg_idx = atoi(optarg);
			break;
		case 'p': // bdg_port
			breq->bdg_port = atoi(optarg);
			break;
		case 'd': // dst
			breq->nr_dst = atoi(optarg);
			break;
		case 'm': // addr
			sscanf(optarg, "%0x:%0x:%0x:%0x:%0x:%0x", 
					&breq->addr[0], &breq->addr[1], &breq->addr[2],
					&breq->addr[3], &breq->addr[4], &breq->addr[5]);
			bdgenv |= BDGCTL_ENV_MAC;
			break;
		case '4': // ipv4 addr
			sscanf(optarg, "%d.%d.%d.%d", 
					&breq->addr[0], &breq->addr[1], &breq->addr[2],
					&breq->addr[3]);
			bdgenv |= BDGCTL_ENV_IP4;
			break;
		case '6': // ipv6 addr
			fprintf(stderr, "ipv6 not supported yet");
			break;
		case 'V': // ipv6 addr
			vport = optarg;
			break;

		}
	}
	return breq;
}

int main(int argc, char **argv)
{
	int ret = -1, i;
	struct bdgreq *breq;

	asprintf(&bdgfn, "/dev/%s", *(++argv));
	++argv;
	argc -= 2;
	
    for (i = 0; i < ARRAY_SIZE(commands); i++) {
        struct cmd_struct *p = commands + i;
        if (strcmp(p->cmd, *argv))
            continue;

		bdgfninit();
		ret = p->fn( parse_options(--argc, ++argv) );
		fprintf(stderr, "[%s]\n", ret ? "\033[31mnoterror\033[0m" : "\033[32mok\033[0m");
		bdgfnfini();
    }

	if (ret < 0) 
		fprintf(stderr, "unknown command %s\n", *argv);

	free(breq);
	return 0;
}
