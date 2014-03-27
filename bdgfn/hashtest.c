#include <stdint.h>
#include <stdio.h>
#define NM_BDG_HASH 65536
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
mac_rthash(const uint8_t *addr)
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

static __inline uint32_t
ip_rthash4(const uint8_t *addr)
{
		uint32_t c = (addr[3] << 8) | addr[2];
        return (c & BRIDGE_RTHASH_MASK);
}

static __inline uint32_t
ip_rthash3(const uint8_t *addr)
{
        uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0; // hask key

		// pseudo-port = 16 least significant bytes of IP
        b += addr[3] << 16;
		b += addr[2] << 8;

		// ip
        b += addr[3];
		a += addr[2] << 24;
        a += addr[1] << 16;
        a += addr[0] << 8;

        mix(a, b, c);
        return (c & BRIDGE_RTHASH_MASK);
}

static __inline uint32_t
ip_rthash2(const uint8_t *addr)
{
		uint32_t c = addr[3] + addr[2] + addr[1] + addr[0];
        return (c & BRIDGE_RTHASH_MASK);
}

static __inline uint32_t
ip_rthash(const uint8_t *addr)
{
        uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0; // hask key
        
        b += addr[3] << 16;
		a += addr[2];
        a += addr[1];
        a += addr[0];

        mix(a, b, c);
        return (c & BRIDGE_RTHASH_MASK);
}
#undef mix

int main(int argc, char **argv)
{
	uint8_t saddr[] = { 10, 1, 200, 1};
	unsigned int i, j;
	unsigned int l3 = atoi(argv[1])
			, l4 = atoi(argv[2]);

	for (j = 1; j < l3; ++j) {
		saddr[2] = j;
		for (i = 1; i < l4; ++i) {
			saddr[3] = i;
			printf("%d %d\n", saddr[3], ip_rthash4(saddr));
			//printf("%d.%d.%d.%d %d\n", saddr[0], saddr[1], saddr[2], saddr[3], ip_rthash3(saddr));
		}
	}

	return 0;
}
