/*
 * fake_res_query.c — LD_PRELOAD shim that intercepts res_query() and
 * res_init() so that wrapsrv can be tested without a real DNS server.
 *
 * Configure via the environment variable FAKE_SRV_RECORDS, which is a
 * comma-separated list of "host:port:prio:weight" tuples, e.g.:
 *
 *   FAKE_SRV_RECORDS="srv1.example.com:8080:10:1,srv2.example.com:9090:20:1"
 *
 * Records are returned in the order given; priority/weight values are passed
 * through verbatim so the wrapsrv selection algorithm is exercised correctly.
 */

#define _GNU_SOURCE
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Encode a hostname into DNS wire-format labels.
 * Returns the number of bytes written, or -1 on error.
 */
static int
encode_name(const char *name, unsigned char *buf, int buflen)
{
	int total = 0;
	const char *p = name;

	while (*p != '\0') {
		const char *dot = strchr(p, '.');
		int labellen = dot ? (int)(dot - p) : (int)strlen(p);

		if (total + 1 + labellen + 1 > buflen)
			return (-1);

		buf[total++] = (unsigned char)labellen;
		memcpy(buf + total, p, (size_t)labellen);
		total += labellen;

		if (dot == NULL)
			break;
		p = dot + 1;
	}
	buf[total++] = 0; /* root label */
	return (total);
}

/* no-op: suppress real resolver initialisation */
int
res_init(void)
{
	return (0);
}

int
res_query(const char *dname, int class, int type, unsigned char *answer,
	  int anslen)
{
	const char *env;
	char *records, *p, *token;
	int pos = 0, nrecs = 0, qname_offset;
	unsigned char *buf = answer;

	(void)class;
	(void)type;

	env = getenv("FAKE_SRV_RECORDS");
	if (env == NULL || env[0] == '\0')
		env = "localhost:80:10:1";

	/* Count comma-separated records */
	nrecs = 1;
	for (const char *ch = env; *ch != '\0'; ch++)
		if (*ch == ',')
			nrecs++;

	/* ---- DNS Header (12 bytes) ---- */
	buf[pos++] = 0x00;
	buf[pos++] = 0x01; /* ID */
	buf[pos++] = 0x81;
	buf[pos++] = 0x80; /* QR=1 RD=1 RA=1 RCODE=0 */
	buf[pos++] = 0x00;
	buf[pos++] = 0x01; /* QDCOUNT = 1 */
	buf[pos++] = (unsigned char)(nrecs >> 8);
	buf[pos++] = (unsigned char)(nrecs & 0xff); /* ANCOUNT */
	buf[pos++] = 0x00;
	buf[pos++] = 0x00; /* NSCOUNT */
	buf[pos++] = 0x00;
	buf[pos++] = 0x00; /* ARCOUNT */

	/* ---- Question Section ---- */
	qname_offset = pos;
	{
		int n = encode_name(dname, buf + pos, anslen - pos);
		if (n < 0)
			return (-1);
		pos += n;
	}
	buf[pos++] = 0x00;
	buf[pos++] = 0x21; /* QTYPE = SRV */
	buf[pos++] = 0x00;
	buf[pos++] = 0x01; /* QCLASS = IN */

	/* ---- Answer Section ---- */
	records = strdup(env);
	if (records == NULL)
		return (-1);
	p = records;

	while ((token = strsep(&p, ",")) != NULL) {
		char host[256];
		int port = 80, prio = 10, weight = 1;
		unsigned char rdata[512];
		int rdpos = 0, n;

		memset(host, 0, sizeof(host));
		if (sscanf(token, "%255[^:]:%d:%d:%d", host, &port, &prio,
			   &weight) < 1) {
			/* Malformed record entry; skip it. */
			continue;
		}

		/* NAME: pointer to QNAME in question section */
		buf[pos++] = 0xC0;
		buf[pos++] = (unsigned char)qname_offset;
		buf[pos++] = 0x00;
		buf[pos++] = 0x21; /* TYPE = SRV */
		buf[pos++] = 0x00;
		buf[pos++] = 0x01; /* CLASS = IN */
		buf[pos++] = 0x00;
		buf[pos++] = 0x00;
		buf[pos++] = 0x00;
		buf[pos++] = 0x3c; /* TTL = 60 */

		/* RDATA: priority, weight, port, target */
		rdata[rdpos++] = (unsigned char)(prio >> 8);
		rdata[rdpos++] = (unsigned char)(prio & 0xff);
		rdata[rdpos++] = (unsigned char)(weight >> 8);
		rdata[rdpos++] = (unsigned char)(weight & 0xff);
		rdata[rdpos++] = (unsigned char)(port >> 8);
		rdata[rdpos++] = (unsigned char)(port & 0xff);

		n = encode_name(host, rdata + rdpos,
				(int)(sizeof(rdata)) - rdpos);
		if (n < 0) {
			free(records);
			return (-1);
		}
		rdpos += n;

		/* RDLENGTH */
		buf[pos++] = (unsigned char)(rdpos >> 8);
		buf[pos++] = (unsigned char)(rdpos & 0xff);
		/* RDATA body */
		memcpy(buf + pos, rdata, (size_t)rdpos);
		pos += rdpos;
	}

	free(records);
	return (pos);
}
