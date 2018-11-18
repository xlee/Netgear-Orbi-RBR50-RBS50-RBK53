/***********************************************************************
*
* discovery.c
*
* Perform PPPoE discovery
*
* Copyright (C) 1999 by Roaring Penguin Software Inc.
*
***********************************************************************/

static char const RCSID[] =
"$Id: discovery.c,v 1.6 2008/06/15 04:35:50 paulus Exp $";

#define _GNU_SOURCE 1
#include "pppoe.h"
#include "pppd/config.h"
#include "pppd/pppd.h"
#include "pppd/fsm.h"
#include "pppd/lcp.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/timeb.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef USE_LINUX_PACKET
#include <sys/ioctl.h>
#include <fcntl.h>
#endif

#include <signal.h>
#define MAX_AC_NAME_LEN	126
#define MAX_AC_SIZE	8
#define MAX_AC_NAME_RETRY 10

struct pppoe_ac_name
{
	unsigned short success;

	char ac_name[MAX_AC_NAME_LEN];
	int fail_counter;
};

struct pppoe_ac_name ac_names[MAX_AC_SIZE];

struct pppoe_ac_name *cur_ac_name;

int  pppoe_ac_num = 0;

int parse_ac_name(char *name, UINT16_t len)
{
	int i;

	for (i = 0; i < pppoe_ac_num; i++) {
		if (strncmp(name, ac_names[i].ac_name, len) == 0)
		{
			if (ac_names[i].success == 1)
				return 1;
			else if ( ac_names[i].success == 0 && ac_names[i].fail_counter < MAX_AC_NAME_RETRY)
			{
				ac_names[i].fail_counter++;
				return 0;
			}
			else
			{
				/*If auth failed counter is MAX_AC_NAME_RETRY, we should remove this ac name from array*/
				pppoe_ac_num--;
				memcpy(&ac_names[i], &ac_names[pppoe_ac_num], sizeof(struct pppoe_ac_name));
				memset(&ac_names[pppoe_ac_num], 0, sizeof(struct pppoe_ac_name));
				return 0;
			}
		}
	}

	/* If the AC table is full, we assume the AC name is acceptable. */
	if (pppoe_ac_num == MAX_AC_SIZE)
		return 1;
	
	cur_ac_name = &ac_names[pppoe_ac_num++];
	cur_ac_name->success = 1;
	cur_ac_name->fail_counter = 0;
	strncpy(cur_ac_name->ac_name, name, len);

	return 1;
}

void cur_ac_fail(void)
{
	if (cur_ac_name) {
		cur_ac_name->success = 0;
		cur_ac_name->fail_counter++;
		cur_ac_name = NULL; /* Clear it! :) */
	}
}

void reset_cur_ac(void)
{
	cur_ac_name = NULL;
}


/* Calculate time remaining until *exp, return 0 if now >= *exp */
static int time_left(struct timeval *diff, struct timeval *exp)
{
    struct timeval now;

    if (gettimeofday(&now, NULL) < 0) {
	error("gettimeofday: %m");
	return 0;
    }

    if (now.tv_sec > exp->tv_sec
	|| (now.tv_sec == exp->tv_sec && now.tv_usec >= exp->tv_usec))
	return 0;

    diff->tv_sec = exp->tv_sec - now.tv_sec;
    diff->tv_usec = exp->tv_usec - now.tv_usec;
    if (diff->tv_usec < 0) {
	diff->tv_usec += 1000000;
	--diff->tv_sec;
    }

    return 1;
}

/**********************************************************************
*%FUNCTION: parseForHostUniq
*%ARGUMENTS:
* type -- tag type
* len -- tag length
* data -- tag data.
* extra -- user-supplied pointer.  This is assumed to be a pointer to int.
*%RETURNS:
* Nothing
*%DESCRIPTION:
* If a HostUnique tag is found which matches our PID, sets *extra to 1.
***********************************************************************/
static void
parseForHostUniq(UINT16_t type, UINT16_t len, unsigned char *data,
		 void *extra)
{
    int *val = (int *) extra;
    if (type == TAG_HOST_UNIQ && len == sizeof(pid_t)) {
	pid_t tmp;
	memcpy(&tmp, data, len);
	if (tmp == getpid()) {
	    *val = 1;
	}
    }
}

/**********************************************************************
*%FUNCTION: packetIsForMe
*%ARGUMENTS:
* conn -- PPPoE connection info
* packet -- a received PPPoE packet
*%RETURNS:
* 1 if packet is for this PPPoE daemon; 0 otherwise.
*%DESCRIPTION:
* If we are using the Host-Unique tag, verifies that packet contains
* our unique identifier.
***********************************************************************/
static int
packetIsForMe(PPPoEConnection *conn, PPPoEPacket *packet)
{
    int forMe = 0;

    /* If packet is not directed to our MAC address, forget it */
    if (memcmp(packet->ethHdr.h_dest, conn->myEth, ETH_ALEN)) return 0;

    /* If we're not using the Host-Unique tag, then accept the packet */
    if (!conn->useHostUniq) return 1;

    parsePacket(packet, parseForHostUniq, &forMe);
    return forMe;
}

/**********************************************************************
*%FUNCTION: parsePADOTags
*%ARGUMENTS:
* type -- tag type
* len -- tag length
* data -- tag data
* extra -- extra user data.  Should point to a PacketCriteria structure
*          which gets filled in according to selected AC name and service
*          name.
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Picks interesting tags out of a PADO packet
***********************************************************************/
static void
parsePADOTags(UINT16_t type, UINT16_t len, unsigned char *data,
	      void *extra)
{
    struct PacketCriteria *pc = (struct PacketCriteria *) extra;
    PPPoEConnection *conn = pc->conn;
    UINT16_t mru;
    int i;

    switch(type) {
    case TAG_AC_NAME:
	pc->seenACName = 1;
	if (conn->printACNames) {
	    info("Access-Concentrator: %.*s", (int) len, data);
	}

	printf("Rcv'd PPPoE AC name: %.*s\n", (int) len, data);
	if (conn->acName == NULL)
	    pc->acNameOK = parse_ac_name((char *)data, len);

	if (conn->acName && len == strlen(conn->acName) &&
	    !strncmp((char *) data, conn->acName, len)) {
	    pc->acNameOK = 1;
	}
	break;
    case TAG_SERVICE_NAME:
	pc->seenServiceName = 1;
	if (conn->serviceName && len == strlen(conn->serviceName) &&
	    !strncmp((char *) data, conn->serviceName, len)) {
	    pc->serviceNameOK = 1;
	}
	if (conn->serviceName == NULL) {
	    conn->srvName.type = htons(type);
	    conn->srvName.length = htons(len);
	    memcpy(conn->srvName.payload, data, len);
	}
	break;
    case TAG_AC_COOKIE:
	conn->cookie.type = htons(type);
	conn->cookie.length = htons(len);
	memcpy(conn->cookie.payload, data, len);
	break;
    case TAG_RELAY_SESSION_ID:
	conn->relayId.type = htons(type);
	conn->relayId.length = htons(len);
	memcpy(conn->relayId.payload, data, len);
	break;
    case TAG_PPP_MAX_PAYLOAD:
	if (len == sizeof(mru)) {
	    memcpy(&mru, data, sizeof(mru));
	    mru = ntohs(mru);
	    if (mru >= ETH_PPPOE_MTU) {
		if (lcp_allowoptions[0].mru > mru)
		    lcp_allowoptions[0].mru = mru;
		if (lcp_wantoptions[0].mru > mru)
		    lcp_wantoptions[0].mru = mru;
		conn->seenMaxPayload = 1;
	    }
	}
	break;
    case TAG_SERVICE_NAME_ERROR:
	error("PADO: Service-Name-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    case TAG_AC_SYSTEM_ERROR:
	error("PADO: System-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    case TAG_GENERIC_ERROR:
	error("PADO: Generic-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    }
}

/**********************************************************************
*%FUNCTION: parsePADSTags
*%ARGUMENTS:
* type -- tag type
* len -- tag length
* data -- tag data
* extra -- extra user data (pointer to PPPoEConnection structure)
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Picks interesting tags out of a PADS packet
***********************************************************************/
static void
parsePADSTags(UINT16_t type, UINT16_t len, unsigned char *data,
	      void *extra)
{
    PPPoEConnection *conn = (PPPoEConnection *) extra;
    UINT16_t mru;
    switch(type) {
    case TAG_SERVICE_NAME:
	dbglog("PADS: Service-Name: '%.*s'", (int) len, data);
	break;
    case TAG_PPP_MAX_PAYLOAD:
	if (len == sizeof(mru)) {
	    memcpy(&mru, data, sizeof(mru));
	    mru = ntohs(mru);
	    if (mru >= ETH_PPPOE_MTU) {
		if (lcp_allowoptions[0].mru > mru)
		    lcp_allowoptions[0].mru = mru;
		if (lcp_wantoptions[0].mru > mru)
		    lcp_wantoptions[0].mru = mru;
		conn->seenMaxPayload = 1;
	    }
	}
	break;
    case TAG_SERVICE_NAME_ERROR:
	error("PADS: Service-Name-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    case TAG_AC_SYSTEM_ERROR:
	error("PADS: System-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    case TAG_GENERIC_ERROR:
	error("PADS: Generic-Error: %.*s", (int) len, data);
	conn->error = 1;
	break;
    case TAG_RELAY_SESSION_ID:
	conn->relayId.type = htons(type);
	conn->relayId.length = htons(len);
	memcpy(conn->relayId.payload, data, len);
	break;
    }
}

/***********************************************************************
*%FUNCTION: sendPADI
*%ARGUMENTS:
* conn -- PPPoEConnection structure
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Sends a PADI packet
***********************************************************************/
static void
sendPADI(PPPoEConnection *conn)
{
    PPPoEPacket packet;
    unsigned char *cursor = packet.payload;
    PPPoETag *svc = (PPPoETag *) (&packet.payload);
    UINT16_t namelen = 0;
    UINT16_t plen;
    int omit_service_name = 0;

    if (conn->serviceName) {
	namelen = (UINT16_t) strlen(conn->serviceName);
	if (!strcmp(conn->serviceName, "NO-SERVICE-NAME-NON-RFC-COMPLIANT")) {
	    omit_service_name = 1;
	}
    }

    /* Set destination to Ethernet broadcast address */
    memset(packet.ethHdr.h_dest, 0xFF, ETH_ALEN);
    memcpy(packet.ethHdr.h_source, conn->myEth, ETH_ALEN);

    packet.ethHdr.h_proto = htons(Eth_PPPOE_Discovery);
    packet.vertype = PPPOE_VER_TYPE(1, 1);
    packet.code = CODE_PADI;
    packet.session = 0;

    if (!omit_service_name) {
	plen = TAG_HDR_SIZE + namelen;
	CHECK_ROOM(cursor, packet.payload, plen);

	svc->type = TAG_SERVICE_NAME;
	svc->length = htons(namelen);

	if (conn->serviceName) {
	    memcpy(svc->payload, conn->serviceName, strlen(conn->serviceName));
	}
	cursor += namelen + TAG_HDR_SIZE;
    } else {
	plen = 0;
    }

    /* If we're using Host-Uniq, copy it over */
    if (conn->useHostUniq) {
	PPPoETag hostUniq;
	pid_t pid = getpid();
	hostUniq.type = htons(TAG_HOST_UNIQ);
	hostUniq.length = htons(sizeof(pid));
	memcpy(hostUniq.payload, &pid, sizeof(pid));
	CHECK_ROOM(cursor, packet.payload, sizeof(pid) + TAG_HDR_SIZE);
	memcpy(cursor, &hostUniq, sizeof(pid) + TAG_HDR_SIZE);
	cursor += sizeof(pid) + TAG_HDR_SIZE;
	plen += sizeof(pid) + TAG_HDR_SIZE;
    }

    /* Add our maximum MTU/MRU */
    if (MIN(lcp_allowoptions[0].mru, lcp_wantoptions[0].mru) > ETH_PPPOE_MTU) {
	PPPoETag maxPayload;
	UINT16_t mru = htons(MIN(lcp_allowoptions[0].mru, lcp_wantoptions[0].mru));
	maxPayload.type = htons(TAG_PPP_MAX_PAYLOAD);
	maxPayload.length = htons(sizeof(mru));
	memcpy(maxPayload.payload, &mru, sizeof(mru));
	CHECK_ROOM(cursor, packet.payload, sizeof(mru) + TAG_HDR_SIZE);
	memcpy(cursor, &maxPayload, sizeof(mru) + TAG_HDR_SIZE);
	cursor += sizeof(mru) + TAG_HDR_SIZE;
	plen += sizeof(mru) + TAG_HDR_SIZE;
    }

    packet.length = htons(plen);

    sendPacket(conn, conn->discoverySocket, &packet, (int) (plen + HDR_SIZE));
}

/**********************************************************************
*%FUNCTION: waitForPADO
*%ARGUMENTS:
* conn -- PPPoEConnection structure
* timeout -- how long to wait (in seconds)
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Waits for a PADO packet and copies useful information
***********************************************************************/
void
waitForPADO(PPPoEConnection *conn, int timeout)
{
    fd_set readable;
    int r;
    struct timeval tv;
    struct timeval expire_at;

    PPPoEPacket packet;
    int len;

    struct PacketCriteria pc;
    pc.conn          = conn;
    pc.acNameOK      = (conn->acName)      ? 0 : 1;
    pc.serviceNameOK = (conn->serviceName) ? 0 : 1;
    pc.seenACName    = 0;
    pc.seenServiceName = 0;
    conn->seenMaxPayload = 0;
    conn->error = 0;

    if (gettimeofday(&expire_at, NULL) < 0) {
	error("gettimeofday (waitForPADO): %m");
	return;
    }
    expire_at.tv_sec += timeout;

    do {
	if (BPF_BUFFER_IS_EMPTY) {
	    if (!time_left(&tv, &expire_at))
		return;		/* Timed out */

	    FD_ZERO(&readable);
	    FD_SET(conn->discoverySocket, &readable);

	    while(1) {
		r = select(conn->discoverySocket+1, &readable, NULL, NULL, &tv);
		if (r >= 0 || errno != EINTR) break;
	    }
	    if (r < 0) {
		error("select (waitForPADO): %m");
		return;
	    }
	    if (r == 0)
		return;		/* Timed out */
	}

	/* Get the packet */
	receivePacket(conn->discoverySocket, &packet, &len);

	/* Check length */
	if (ntohs(packet.length) + HDR_SIZE > len) {
	    error("Bogus PPPoE length field (%u)",
		   (unsigned int) ntohs(packet.length));
	    continue;
	}

#ifdef USE_BPF
	/* If it's not a Discovery packet, loop again */
	if (etherType(&packet) != Eth_PPPOE_Discovery) continue;
#endif

	/* If it's not for us, loop again */
	if (!packetIsForMe(conn, &packet)) continue;

	if (packet.code == CODE_PADO) {
	    if (BROADCAST(packet.ethHdr.h_source)) {
		printf(stderr, "Ignoring PADO packet from broadcast MAC address");
		continue;
	    }
	    if (conn->req_peer
		&& memcmp(packet.ethHdr.h_source, conn->req_peer_mac, ETH_ALEN) != 0) {
		warn("Ignoring PADO packet from wrong MAC address");
		continue;
	    }

	    /* Reset these types before parsing. */
	    conn->srvName.type = 0;
	    conn->cookie.type = 0;
	    conn->relayId.type = 0;

	    if (parsePacket(&packet, parsePADOTags, &pc) < 0)
		return;
	    if (conn->error)
		return;
	    if (!pc.seenACName) {
		error("Ignoring PADO packet with no AC-Name tag");
		continue;
	    }
	    if (!pc.seenServiceName) {
		error("Ignoring PADO packet with no Service-Name tag");
		continue;
	    }
	    conn->numPADOs++;
	    if (pc.acNameOK && pc.serviceNameOK) {
		memcpy(conn->peerEth, packet.ethHdr.h_source, ETH_ALEN);
		conn->discoveryState = STATE_RECEIVED_PADO;
		break;
	    }
	}
    } while (conn->discoveryState != STATE_RECEIVED_PADO);
}

/***********************************************************************
*%FUNCTION: sendPADR
*%ARGUMENTS:
* conn -- PPPoE connection structur
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Sends a PADR packet
***********************************************************************/
static void
sendPADR(PPPoEConnection *conn)
{
    PPPoEPacket packet;
    PPPoETag *svc = (PPPoETag *) packet.payload;
    unsigned char *cursor = packet.payload;

    UINT16_t namelen = 0;
    UINT16_t plen;

    if (conn->serviceName) {
	namelen = (UINT16_t) strlen(conn->serviceName);
    } else if (conn->srvName.type) {
	namelen = ntohs(conn->srvName.length);
	}
    plen = TAG_HDR_SIZE + namelen;
    CHECK_ROOM(cursor, packet.payload, plen);

    memcpy(packet.ethHdr.h_dest, conn->peerEth, ETH_ALEN);
    memcpy(packet.ethHdr.h_source, conn->myEth, ETH_ALEN);

    packet.ethHdr.h_proto = htons(Eth_PPPOE_Discovery);
    packet.vertype = PPPOE_VER_TYPE(1, 1);
    packet.code = CODE_PADR;
    packet.session = 0;

    svc->type = TAG_SERVICE_NAME;
    svc->length = htons(namelen);
    if (conn->serviceName) {
	memcpy(svc->payload, conn->serviceName, namelen);
    } else if (conn->srvName.type) {
	memcpy(svc->payload, conn->srvName.payload, namelen);
    }
    cursor += namelen + TAG_HDR_SIZE;

    /* If we're using Host-Uniq, copy it over */
    if (conn->useHostUniq) {
	PPPoETag hostUniq;
	pid_t pid = getpid();
	hostUniq.type = htons(TAG_HOST_UNIQ);
	hostUniq.length = htons(sizeof(pid));
	memcpy(hostUniq.payload, &pid, sizeof(pid));
	CHECK_ROOM(cursor, packet.payload, sizeof(pid)+TAG_HDR_SIZE);
	memcpy(cursor, &hostUniq, sizeof(pid) + TAG_HDR_SIZE);
	cursor += sizeof(pid) + TAG_HDR_SIZE;
	plen += sizeof(pid) + TAG_HDR_SIZE;
    }

    /* Add our maximum MTU/MRU */
    if (MIN(lcp_allowoptions[0].mru, lcp_wantoptions[0].mru) > ETH_PPPOE_MTU) {
	PPPoETag maxPayload;
	UINT16_t mru = htons(MIN(lcp_allowoptions[0].mru, lcp_wantoptions[0].mru));
	maxPayload.type = htons(TAG_PPP_MAX_PAYLOAD);
	maxPayload.length = htons(sizeof(mru));
	memcpy(maxPayload.payload, &mru, sizeof(mru));
	CHECK_ROOM(cursor, packet.payload, sizeof(mru) + TAG_HDR_SIZE);
	memcpy(cursor, &maxPayload, sizeof(mru) + TAG_HDR_SIZE);
	cursor += sizeof(mru) + TAG_HDR_SIZE;
	plen += sizeof(mru) + TAG_HDR_SIZE;
    }

    /* Copy cookie and relay-ID if needed */
    if (conn->cookie.type) {
	CHECK_ROOM(cursor, packet.payload,
		   ntohs(conn->cookie.length) + TAG_HDR_SIZE);
	memcpy(cursor, &conn->cookie, ntohs(conn->cookie.length) + TAG_HDR_SIZE);
	cursor += ntohs(conn->cookie.length) + TAG_HDR_SIZE;
	plen += ntohs(conn->cookie.length) + TAG_HDR_SIZE;
    }

    if (conn->relayId.type) {
	CHECK_ROOM(cursor, packet.payload,
		   ntohs(conn->relayId.length) + TAG_HDR_SIZE);
	memcpy(cursor, &conn->relayId, ntohs(conn->relayId.length) + TAG_HDR_SIZE);
	cursor += ntohs(conn->relayId.length) + TAG_HDR_SIZE;
	plen += ntohs(conn->relayId.length) + TAG_HDR_SIZE;
    }

    packet.length = htons(plen);
    sendPacket(conn, conn->discoverySocket, &packet, (int) (plen + HDR_SIZE));
}

/**********************************************************************
*%FUNCTION: waitForPADS
*%ARGUMENTS:
* conn -- PPPoE connection info
* timeout -- how long to wait (in seconds)
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Waits for a PADS packet and copies useful information
***********************************************************************/
static void
waitForPADS(PPPoEConnection *conn, int timeout)
{
    fd_set readable;
    int r;
    struct timeval tv;
    struct timeval expire_at;

    PPPoEPacket packet;
    int len;

    if (gettimeofday(&expire_at, NULL) < 0) {
	error("gettimeofday (waitForPADS): %m");
	return;
    }
    expire_at.tv_sec += timeout;

    conn->error = 0;
    do {
	if (BPF_BUFFER_IS_EMPTY) {
	    if (!time_left(&tv, &expire_at))
		return;		/* Timed out */

	    FD_ZERO(&readable);
	    FD_SET(conn->discoverySocket, &readable);

	    while(1) {
		r = select(conn->discoverySocket+1, &readable, NULL, NULL, &tv);
		if (r >= 0 || errno != EINTR) break;
	    }
	    if (r < 0) {
		error("select (waitForPADS): %m");
		return;
	    }
	    if (r == 0)
		return;		/* Timed out */
	}

	/* Get the packet */
	receivePacket(conn->discoverySocket, &packet, &len);

	/* Check length */
	if (ntohs(packet.length) + HDR_SIZE > len) {
	    error("Bogus PPPoE length field (%u)",
		   (unsigned int) ntohs(packet.length));
	    continue;
	}

#ifdef USE_BPF
	/* If it's not a Discovery packet, loop again */
	if (etherType(&packet) != Eth_PPPOE_Discovery) continue;
#endif

	/* If it's not from the AC, it's not for me */
	if (memcmp(packet.ethHdr.h_source, conn->peerEth, ETH_ALEN)) continue;

	/* If it's not for us, loop again */
	if (!packetIsForMe(conn, &packet)) continue;

	/* Is it PADS?  */
	if (packet.code == CODE_PADS) {
	    /* Parse for goodies */
	    if (parsePacket(&packet, parsePADSTags, conn) < 0)
		return;
	    if (conn->error)
		return;
	    conn->discoveryState = STATE_SESSION;
	    break;
	}
    } while (conn->discoveryState != STATE_SESSION);

    /* Don't bother with ntohs; we'll just end up converting it back... */
    conn->session = packet.session;

    info("PPP session is %d", (int) ntohs(conn->session));

    /* RFC 2516 says session id MUST NOT be zero or 0xFFFF */
    if (ntohs(conn->session) == 0 || ntohs(conn->session) == 0xFFFF) {
	error("Access concentrator used a session value of %x -- the AC is violating RFC 2516", (unsigned int) ntohs(conn->session));
    }
}

/**********************************************************************
*%FUNCTION: discovery
*%ARGUMENTS:
* conn -- PPPoE connection info structure
*%RETURNS:
* Nothing
*%DESCRIPTION:
* Performs the PPPoE discovery phase
***********************************************************************/
void
discovery(PPPoEConnection *conn)
{
    int padiAttempts = 0;
    int padrAttempts = 0;
    int timeout = conn->discoveryTimeout;
    static struct timeb lasttm = {0, 0, 0, 0}, thistm = {0, 0, 0, 0};
    struct timeval tv = {0, 0};
    unsigned short tm = 0;
    FILE *fp = NULL;
    char cmd[128];
    char old_session[80] = {0};

    do {
	padiAttempts++;
	if (padiAttempts > MAX_PADI_ATTEMPTS) {
	    warn("Timeout waiting for PADO packets");
	    close(conn->discoverySocket);
	    conn->discoverySocket = -1;
	    return;
	}
	ftime(&thistm);
	tm = 0;
	if ((thistm.time == lasttm.time) && (thistm.millitm > lasttm.millitm)) {
	    tm = 1000U - (thistm.millitm - lasttm.millitm);
	} else if ((thistm.time == (lasttm.time + 1)) && (thistm.millitm < lasttm.millitm)) {
	    tm = lasttm.millitm - thistm.millitm;
	}
	if (tm) {
	    memset(&tv, 0x00, sizeof(tv));
	    tv.tv_usec = tm * 1000;
	    select(1, NULL, NULL, NULL, &tv);
	}
	lasttm = thistm;
	sendPADI(conn);
	conn->discoveryState = STATE_SENT_PADI;
	waitForPADO(conn, timeout);

	timeout *= 2;
    } while (conn->discoveryState == STATE_SENT_PADI);

    timeout = conn->discoveryTimeout;
    /* Send PADT before PADI if pppoe sessinID recorded in flash is non zero while restart pppoe */
    if((fp = fopen("/tmp/ppp/pppoe_lst_session","r")) != NULL) {
        fgets(old_session, sizeof(old_session), fp);
        if(old_session != NULL) {
		unsigned int mac[ETH_ALEN];
		int i, ses;
		if (sscanf(old_session, "%d:%x:%x:%x:%x:%x:%x",
                   &ses, &mac[0], &mac[1], &mac[2],
				   &mac[3], &mac[4], &mac[5]) != 7) {
			fatal("Illegal value for pppoe_lst_sess option");
		}
	if(ses != 0) {
		conn->session = htons(ses);
		for (i=0; i<ETH_ALEN; i++) {
			conn->peerEth[i] = (unsigned char) mac[i];
			}
		sendPADT(conn,"Bye-Bye From Home Router!");
		}
	}
	fclose(fp);
	fp = NULL;
    }
    do {
	padrAttempts++;
	if (padrAttempts > MAX_PADI_ATTEMPTS) {
	    warn("Timeout waiting for PADS packets");
	    close(conn->discoverySocket);
	    conn->discoverySocket = -1;
	    return;
	}
	sendPADR(conn);
	conn->discoveryState = STATE_SENT_PADR;
	waitForPADS(conn, timeout);
#if 0
	timeout *= 2;
#endif
    } while (conn->discoveryState == STATE_SENT_PADR);

    if (!conn->seenMaxPayload) {
	/* RFC 4638: MUST limit MTU/MRU to 1492 */
	if (lcp_allowoptions[0].mru > ETH_PPPOE_MTU)
	    lcp_allowoptions[0].mru = ETH_PPPOE_MTU;
	if (lcp_wantoptions[0].mru > ETH_PPPOE_MTU)
	    lcp_wantoptions[0].mru = ETH_PPPOE_MTU;
    }

    /* We're done. */
    conn->discoveryState = STATE_SESSION;
    /* Write connection sessionID:peerEth to pppoe_session and then record in flash */
    if((fp=(fopen("/tmp/ppp/pppoe_session","w"))) != NULL){
	fprintf(fp,"%d:%x:%x:%x:%x:%x:%x",ntohs(conn->session),conn->peerEth[0],
	        conn->peerEth[1],conn->peerEth[2],conn->peerEth[3],conn->peerEth[4],
		conn->peerEth[5]);
	fclose(fp);
	fp = NULL;
    }
    system(PPPOE_SESSION_SET);

    return;
}