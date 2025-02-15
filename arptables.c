/* Code to take an arptables-style command line and do it. */

/*
 * arptables:
 * Author: Bart De Schuymer <bdschuym@pandora.be>, but
 * almost all code is from the iptables userspace program, which has main
 * authors: Paul.Russell@rustcorp.com.au and mneuling@radlogic.com.au
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
  Currently, only support for specifying hardware addresses for Ethernet
  is available.
  This tool is not luser-proof: you can specify an Ethernet source address
  and set hardware length to something different than 6, f.e.
*/

#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <arptables.h>
#include <fcntl.h>
#include <sys/wait.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ARPT_LIB_DIR
#define ARPT_LIB_DIR "/usr/local/lib/arptables"
#endif

#ifndef PROC_SYS_MODPROBE
#define PROC_SYS_MODPROBE "/proc/sys/kernel/modprobe"
#endif

#define FMT_NUMERIC	0x0001
#define FMT_NOCOUNTS	0x0002
#define FMT_KILOMEGAGIGA 0x0004
#define FMT_OPTIONS	0x0008
#define FMT_NOTABLE	0x0010
#define FMT_NOTARGET	0x0020
#define FMT_VIA		0x0040
#define FMT_NONEWLINE	0x0080
#define FMT_LINENUMBERS 0x0100

#define FMT_PRINT_RULE (FMT_NOCOUNTS | FMT_OPTIONS | FMT_VIA \
			| FMT_NUMERIC | FMT_NOTABLE)
#define FMT(tab,notab) ((format) & FMT_NOTABLE ? (notab) : (notab))


#define CMD_NONE		0x0000U
#define CMD_INSERT		0x0001U
#define CMD_DELETE		0x0002U
#define CMD_DELETE_NUM		0x0004U
#define CMD_REPLACE		0x0008U
#define CMD_APPEND		0x0010U
#define CMD_LIST		0x0020U
#define CMD_FLUSH		0x0040U
#define CMD_ZERO		0x0080U
#define CMD_NEW_CHAIN		0x0100U
#define CMD_DELETE_CHAIN	0x0200U
#define CMD_SET_POLICY		0x0400U
#define CMD_CHECK		0x0800U
#define CMD_RENAME_CHAIN	0x1000U
#define NUMBER_OF_CMD	13
static const char cmdflags[] = { 'I', 'D', 'D', 'R', 'A', 'L', 'F', 'Z',
				 'N', 'X', 'P', 'E' };

#define OPTION_OFFSET 256

#define OPT_NONE	0x00000U
#define OPT_NUMERIC	0x00001U
#define OPT_S_IP	0x00002U
#define OPT_D_IP	0x00004U
#define OPT_S_MAC	0x00008U
#define OPT_D_MAC	0x00010U
#define OPT_H_LENGTH	0x00020U
#define OPT_P_LENGTH	0x00040U
#define OPT_OPCODE	0x00080U
#define OPT_H_TYPE	0x00100U
#define OPT_P_TYPE	0x00200U
#define OPT_JUMP	0x00400U
#define OPT_VERBOSE	0x00800U
#define OPT_VIANAMEIN	0x01000U
#define OPT_VIANAMEOUT	0x02000U
#define OPT_LINENUMBERS 0x04000U
#define OPT_COUNTERS	0x08000U
#define NUMBER_OF_OPT	16
static const char optflags[NUMBER_OF_OPT]
= { 'n', 's', 'd', 2, 3, 7, 8, 4, 5, 6, 'j', 'v', 'i', 'o', '0', 'c'};

static struct option original_opts[] = {
	{ "append", 1, 0, 'A' },
	{ "delete", 1, 0,  'D' },
	{ "insert", 1, 0,  'I' },
	{ "replace", 1, 0,  'R' },
	{ "list", 2, 0,  'L' },
	{ "flush", 2, 0,  'F' },
	{ "zero", 2, 0,  'Z' },
	{ "new-chain", 1, 0,  'N' },
	{ "delete-chain", 2, 0,  'X' },
	{ "rename-chain", 1, 0,  'E' },
	{ "policy", 1, 0,  'P' },
	{ "source-ip", 1, 0, 's' },
	{ "destination-ip", 1, 0,  'd' },
	{ "src-ip", 1, 0,  's' },
	{ "dst-ip", 1, 0,  'd' },
	{ "source-mac", 1, 0, 2},
	{ "destination-mac", 1, 0, 3},
	{ "src-mac", 1, 0, 2},
	{ "dst-mac", 1, 0, 3},
	{ "h-length", 1, 0,  'l' },
	{ "p-length", 1, 0,  8 },
	{ "opcode", 1, 0,  4 },
	{ "h-type", 1, 0,  5 },
	{ "proto-type", 1, 0,  6 },
	{ "in-interface", 1, 0, 'i' },
	{ "jump", 1, 0, 'j' },
	{ "table", 1, 0, 't' },
	{ "match", 1, 0, 'm' },
	{ "numeric", 0, 0, 'n' },
	{ "out-interface", 1, 0, 'o' },
	{ "verbose", 0, 0, 'v' },
	{ "exact", 0, 0, 'x' },
	{ "version", 0, 0, 'V' },
	{ "help", 2, 0, 'h' },
	{ "line-numbers", 0, 0, '0' },
	{ "modprobe", 1, 0, 'M' },
	{ "set-counters", 1, 0, 'c' },
	{ 0 }
};

int RUNTIME_NF_ARP_NUMHOOKS = 3;

/*#ifndef __OPTIMIZE__
struct arpt_entry_target *
arpt_get_target(struct arpt_entry *e)
{
	return (void *)e + e->target_offset;
}
#endif*/

static struct option *opts = original_opts;
static unsigned int global_option_offset = 0;

/* Table of legal combinations of commands and options.  If any of the
 * given commands make an option legal, that option is legal (applies to
 * CMD_LIST and CMD_ZERO only).
 * Key:
 *  +  compulsory
 *  x  illegal
 *     optional
 */

static char commands_v_options[NUMBER_OF_CMD][NUMBER_OF_OPT] =
/* Well, it's better than "Re: Linux vs FreeBSD" */
{
	/*     -n  -s  -d  -p  -j  -v  -x  -i  -o  -f  --line */
/*INSERT*/    {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*DELETE*/    {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*DELETE_NUM*/{' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*REPLACE*/   {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*APPEND*/    {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*LIST*/      {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*FLUSH*/     {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*ZERO*/      {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*NEW_CHAIN*/ {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*DEL_CHAIN*/ {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*SET_POLICY*/{' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*CHECK*/     {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
/*RENAME*/    {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '}
};

static int inverse_for_options[NUMBER_OF_OPT] =
{
/* -n */ 0,
/* -s */ ARPT_INV_SRCIP,
/* -d */ ARPT_INV_TGTIP,
/* 2 */ ARPT_INV_SRCDEVADDR,
/* 3 */ ARPT_INV_TGTDEVADDR,
/* -l */ ARPT_INV_ARPHLN,
/* 8 */ 0,
/* 4 */ ARPT_INV_ARPOP,
/* 5 */ ARPT_INV_ARPHRD,
/* 6 */ ARPT_INV_ARPPRO,
/* -j */ 0,
/* -v */ 0,
/* -i */ ARPT_INV_VIA_IN,
/* -o */ ARPT_INV_VIA_OUT,
/*--line*/ 0,
/* -c */ 0,
};

const char *program_version = ARPTABLES_VERSION;
const char *program_name;

/* Keeping track of external matches and targets: linked lists.  */
struct arptables_match *arptables_matches = NULL;
struct arptables_target *arptables_targets = NULL;

/* Extra debugging from libarptc */
extern void dump_entries(const arptc_handle_t handle);

/* A few hardcoded protocols for 'all' and in case the user has no
   /etc/protocols */
struct pprot {
	char *name;
	uint8_t num;
};

/* Primitive headers... */
/* defined in netinet/in.h */
#if 0
#ifndef IPPROTO_ESP
#define IPPROTO_ESP 50
#endif
#ifndef IPPROTO_AH
#define IPPROTO_AH 51
#endif
#endif

/***********************************************/
/* ARPTABLES SPECIFIC NEW FUNCTIONS ADDED HERE */
/***********************************************/

unsigned char mac_type_unicast[ETH_ALEN] =   {0,0,0,0,0,0};
unsigned char msk_type_unicast[ETH_ALEN] =   {1,0,0,0,0,0};
unsigned char mac_type_multicast[ETH_ALEN] = {1,0,0,0,0,0};
unsigned char msk_type_multicast[ETH_ALEN] = {1,0,0,0,0,0};
unsigned char mac_type_broadcast[ETH_ALEN] = {255,255,255,255,255,255};
unsigned char msk_type_broadcast[ETH_ALEN] = {255,255,255,255,255,255};

/* a few names */
static char *opcodes[] =
{
	"Request",
	"Reply",
	"Request_Reverse",
	"Reply_Reverse",
	"DRARP_Request",
	"DRARP_Reply",
	"DRARP_Error",
	"InARP_Request",
	"ARP_NAK",
};
#define NUMOPCODES 9

/*
 * put the mac address into 6 (ETH_ALEN) bytes
 */
int getmac_and_mask(char *from, char *to, char *mask)
{
	char *p;
	int i;
	struct ether_addr *addr;

	if (strcasecmp(from, "Unicast") == 0) {
		memcpy(to, mac_type_unicast, ETH_ALEN);
		memcpy(mask, msk_type_unicast, ETH_ALEN);
		return 0;
	}
	if (strcasecmp(from, "Multicast") == 0) {
		memcpy(to, mac_type_multicast, ETH_ALEN);
		memcpy(mask, msk_type_multicast, ETH_ALEN);
		return 0;
	}
	if (strcasecmp(from, "Broadcast") == 0) {
		memcpy(to, mac_type_broadcast, ETH_ALEN);
		memcpy(mask, msk_type_broadcast, ETH_ALEN);
		return 0;
	}
	if ( (p = strrchr(from, '/')) != NULL) {
		*p = '\0';
		if (!(addr = ether_aton(p + 1)))
			return -1;
		memcpy(mask, addr, ETH_ALEN);
	} else
		memset(mask, 0xff, ETH_ALEN);
	if (!(addr = ether_aton(from)))
		return -1;
	memcpy(to, addr, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++)
		to[i] &= mask[i];
	return 0;
}

int getlength_and_mask(char *from, uint8_t *to, uint8_t *mask)
{
	char *p, *buffer;
	int i;

	if ( (p = strrchr(from, '/')) != NULL) {
		*p = '\0';
		i = strtol(p+1, &buffer, 10);
		if (*buffer != '\0' || i < 0 || i > 255)
			return -1;
		*mask = (uint8_t)i;
	} else
		*mask = 255;
	i = strtol(from, &buffer, 10);
	if (*buffer != '\0' || i < 0 || i > 255)
		return -1;
	*to = (uint8_t)i;
	return 0;
}

int get16_and_mask(char *from, uint16_t *to, uint16_t *mask, int base)
{
	char *p, *buffer;
	int i;

	if ( (p = strrchr(from, '/')) != NULL) {
		*p = '\0';
		i = strtol(p+1, &buffer, base);
		if (*buffer != '\0' || i < 0 || i > 65535)
			return -1;
		*mask = htons((uint16_t)i);
	} else
		*mask = 65535;
	i = strtol(from, &buffer, base);
	if (*buffer != '\0' || i < 0 || i > 65535)
		return -1;
	*to = htons((uint16_t)i);
	return 0;
}

void print_mac(const unsigned char *mac, int l)
{
	int j;

	for (j = 0; j < l; j++)
		printf("%02x%s", mac[j],
			(j==l-1) ? "" : ":");
}

void print_mac_and_mask(const unsigned char *mac, const unsigned char *mask, int l)
{
	int i;

	print_mac(mac, l);
	for (i = 0; i < l ; i++)
		if (mask[i] != 255)
			break;
	if (i == l)
		return;
	printf("/");
	print_mac(mask, l);
}

/*********************************************/
/* ARPTABLES SPECIFIC NEW FUNCTIONS END HERE */
/*********************************************/

struct in_addr *
dotted_to_addr(const char *dotted)
{
	static struct in_addr addr;
	unsigned char *addrp;
	char *p, *q;
	unsigned int onebyte;
	int i;
	char buf[20];

	/* copy dotted string, because we need to modify it */
	strncpy(buf, dotted, sizeof(buf) - 1);
	addrp = (unsigned char *) &(addr.s_addr);

	p = buf;
	for (i = 0; i < 3; i++) {
		if ((q = strchr(p, '.')) == NULL)
			return (struct in_addr *) NULL;

		*q = '\0';
		if (string_to_number(p, 0, 255, &onebyte) == -1)
			return (struct in_addr *) NULL;

		addrp[i] = (unsigned char) onebyte;
		p = q + 1;
	}

	/* we've checked 3 bytes, now we check the last one */
	if (string_to_number(p, 0, 255, &onebyte) == -1)
		return (struct in_addr *) NULL;

	addrp[3] = (unsigned char) onebyte;

	return &addr;
}

static struct in_addr *
network_to_addr(const char *name)
{
	struct netent *net;
	static struct in_addr addr;

	if ((net = getnetbyname(name)) != NULL) {
		if (net->n_addrtype != AF_INET)
			return (struct in_addr *) NULL;
		addr.s_addr = htonl((unsigned long) net->n_net);
		return &addr;
	}

	return (struct in_addr *) NULL;
}

static void
inaddrcpy(struct in_addr *dst, struct in_addr *src)
{
	/* memcpy(dst, src, sizeof(struct in_addr)); */
	dst->s_addr = src->s_addr;
}

void
exit_error(enum exittype status, char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fprintf(stderr, "%s v%s: ", program_name, program_version);
	vfprintf(stderr, msg, args);
	va_end(args);
	fprintf(stderr, "\n");
	if (status == PARAMETER_PROBLEM)
		exit_tryhelp(status);
	if (status == VERSION_PROBLEM)
		fprintf(stderr,
			"Perhaps arptables or your kernel needs to be upgraded.\n");
	exit(status);
}

void
exit_tryhelp(int status)
{
	fprintf(stderr, "Try `%s -h' or '%s --help' for more information.\n",
			program_name, program_name );
	exit(status);
}

void
exit_printhelp(void)
{
	struct arptables_match *m = NULL;
	struct arptables_target *t = NULL;
	int i;

	printf("%s v%s (legacy)\n\n"
"Usage: %s -[AD] chain rule-specification [options]\n"
"       %s -[RI] chain rulenum rule-specification [options]\n"
"       %s -D chain rulenum [options]\n"
"       %s -[LFZ] [chain] [options]\n"
"       %s -[NX] chain\n"
"       %s -E old-chain-name new-chain-name\n"
"       %s -P chain target [options]\n"
"       %s -h (print this help information)\n\n",
	       program_name, program_version, program_name, program_name,
	       program_name, program_name, program_name, program_name,
	       program_name, program_name);

	printf(
"Commands:\n"
"Either long or short options are allowed.\n"
"  --append  -A chain		Append to chain\n"
"  --delete  -D chain		Delete matching rule from chain\n"
"  --delete  -D chain rulenum\n"
"				Delete rule rulenum (1 = first) from chain\n"
"  --insert  -I chain [rulenum]\n"
"				Insert in chain as rulenum (default 1=first)\n"
"  --replace -R chain rulenum\n"
"				Replace rule rulenum (1 = first) in chain\n"
"  --list    -L [chain]		List the rules in a chain or all chains\n"
"  --flush   -F [chain]		Delete all rules in  chain or all chains\n"
"  --zero    -Z [chain]		Zero counters in chain or all chains\n"
"  --new     -N chain		Create a new user-defined chain\n"
"  --delete-chain\n"
"            -X [chain]		Delete a user-defined chain\n"
"  --policy  -P chain target\n"
"				Change policy on chain to target\n"
"  --rename-chain\n"
"            -E old-chain new-chain\n"
"				Change chain name, (moving any references)\n"

"Options:\n"
"  --source-ip	-s [!] address[/mask]\n"
"				source specification\n"
"  --destination-ip -d [!] address[/mask]\n"
"				destination specification\n"
"  --source-mac [!] address[/mask]\n"
"  --destination-mac [!] address[/mask]\n"
"  --h-length   -l   length[/mask] hardware length (nr of bytes)\n"
"  --opcode code[/mask] operation code (2 bytes)\n"
"  --h-type   type[/mask]  hardware type (2 bytes, hexadecimal)\n"
"  --proto-type   type[/mask]  protocol type (2 bytes)\n"
"  --in-interface -i [!] input name[+]\n"
"				network interface name ([+] for wildcard)\n"
"  --out-interface -o [!] output name[+]\n"
"				network interface name ([+] for wildcard)\n"
"  --jump	-j target\n"
"				target for rule (may load target extension)\n"
"  --match	-m match\n"
"				extended match (may load extension)\n"
"  --numeric	-n		numeric output of addresses and ports\n"
"  --table	-t table	table to manipulate (default: `filter')\n"
"  --verbose	-v		verbose mode\n"
"  --line-numbers		print line numbers when listing\n"
"  --exact	-x		expand numbers (display exact values)\n"
"  --modprobe=<command>		try to insert modules using this command\n"
"  --set-counters -c PKTS BYTES	set the counter during insert/append\n"
"[!] --version	-V		print package version.\n");
	printf(" opcode strings: \n");
        for (i = 0; i < NUMOPCODES; i++)
                printf(" %d = %s\n", i + 1, opcodes[i]);
        printf(
" hardware type string: 1 = Ethernet\n"
" protocol type string: 0x800 = IPv4\n");

	/* Print out any special helps. A user might like to be able
	   to add a --help to the commandline, and see expected
	   results. So we call help for all matches & targets */
	for (t=arptables_targets;t;t=t->next) {
		printf("\n");
		t->help();
	}
	for (m=arptables_matches;m;m=m->next) {
		printf("\n");
		m->help();
	}
	exit(0);
}

static void
generic_opt_check(int command, int options)
{
	int i, j, legal = 0;

	/* Check that commands are valid with options.  Complicated by the
	 * fact that if an option is legal with *any* command given, it is
	 * legal overall (ie. -z and -l).
	 */
	for (i = 0; i < NUMBER_OF_OPT; i++) {
		legal = 0; /* -1 => illegal, 1 => legal, 0 => undecided. */

		for (j = 0; j < NUMBER_OF_CMD; j++) {
			if (!(command & (1<<j)))
				continue;

			if (!(options & (1<<i))) {
				if (commands_v_options[j][i] == '+')
					exit_error(PARAMETER_PROBLEM,
						   "You need to supply the `-%c' "
						   "option for this command\n",
						   optflags[i]);
			} else {
				if (commands_v_options[j][i] != 'x')
					legal = 1;
				else if (legal == 0)
					legal = -1;
			}
		}
		if (legal == -1)
			exit_error(PARAMETER_PROBLEM,
				   "Illegal option `-%c' with this command\n",
				   optflags[i]);
	}
}

static char
opt2char(int option)
{
	const char *ptr;
	for (ptr = optflags; option > 1; option >>= 1, ptr++);

	return *ptr;
}

static char
cmd2char(int option)
{
	const char *ptr;
	for (ptr = cmdflags; option > 1; option >>= 1, ptr++);

	return *ptr;
}

static void
add_command(unsigned int *cmd, const int newcmd, const unsigned int othercmds, int invert)
{
	if (invert)
		exit_error(PARAMETER_PROBLEM, "unexpected ! flag");
	if (*cmd & (~othercmds))
		exit_error(PARAMETER_PROBLEM, "Can't use -%c with -%c\n",
			   cmd2char(newcmd), cmd2char(*cmd & (~othercmds)));
	*cmd |= newcmd;
}

int
check_inverse(const char option[], int *invert, int *optind, int argc)
{
	if (option && strcmp(option, "!") == 0) {
		if (*invert)
			exit_error(PARAMETER_PROBLEM,
				   "Multiple `!' flags not allowed");
		*invert = TRUE;
		if (optind) {
			*optind = *optind+1;
			if (argc && *optind > argc)
				exit_error(PARAMETER_PROBLEM,
					   "no argument following `!'");
		}

		return TRUE;
	}
	return FALSE;
}

static void *
fw_calloc(size_t count, size_t size)
{
	void *p;

	if ((p = calloc(count, size)) == NULL) {
		perror("arptables: calloc failed");
		exit(1);
	}
	return p;
}

static void *
fw_malloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL) {
		perror("arptables: malloc failed");
		exit(1);
	}
	return p;
}

static struct in_addr *
host_to_addr(const char *name, unsigned int *naddr)
{
	struct hostent *host;
	struct in_addr *addr;
	unsigned int i;

	*naddr = 0;
	if ((host = gethostbyname(name)) != NULL) {
		if (host->h_addrtype != AF_INET ||
		    host->h_length != sizeof(struct in_addr))
			return (struct in_addr *) NULL;

		while (host->h_addr_list[*naddr] != (char *) NULL)
			(*naddr)++;
		addr = fw_calloc(*naddr, sizeof(struct in_addr));
		for (i = 0; i < *naddr; i++)
			inaddrcpy(&(addr[i]),
				  (struct in_addr *) host->h_addr_list[i]);
		return addr;
	}

	return (struct in_addr *) NULL;
}

static char *
addr_to_host(const struct in_addr *addr)
{
	struct hostent *host;

	if ((host = gethostbyaddr((char *) addr,
				  sizeof(struct in_addr), AF_INET)) != NULL)
		return (char *) host->h_name;

	return (char *) NULL;
}

/*
 *	All functions starting with "parse" should succeed, otherwise
 *	the program fails.
 *	Most routines return pointers to static data that may change
 *	between calls to the same or other routines with a few exceptions:
 *	"host_to_addr", "parse_hostnetwork", and "parse_hostnetworkmask"
 *	return global static data.
*/

struct in_addr *
parse_hostnetwork(const char *name, unsigned int *naddrs)
{
	struct in_addr *addrp, *addrptmp;

	if ((addrptmp = dotted_to_addr(name)) != NULL ||
	    (addrptmp = network_to_addr(name)) != NULL) {
		addrp = fw_malloc(sizeof(struct in_addr));
		inaddrcpy(addrp, addrptmp);
		*naddrs = 1;
		return addrp;
	}
	if ((addrp = host_to_addr(name, naddrs)) != NULL)
		return addrp;

	exit_error(PARAMETER_PROBLEM, "host/network `%s' not found", name);
}

static struct in_addr *
parse_mask(char *mask)
{
	static struct in_addr maskaddr;
	struct in_addr *addrp;
	unsigned int bits;

	if (mask == NULL) {
		/* no mask at all defaults to 32 bits */
		maskaddr.s_addr = 0xFFFFFFFF;
		return &maskaddr;
	}
	if ((addrp = dotted_to_addr(mask)) != NULL)
		/* dotted_to_addr already returns a network byte order addr */
		return addrp;
	if (string_to_number(mask, 0, 32, &bits) == -1)
		exit_error(PARAMETER_PROBLEM,
			   "invalid mask `%s' specified", mask);
	if (bits != 0) {
		maskaddr.s_addr = htonl(0xFFFFFFFF << (32 - bits));
		return &maskaddr;
	}

	maskaddr.s_addr = 0L;
	return &maskaddr;
}

void
parse_hostnetworkmask(const char *name, struct in_addr **addrpp,
		      struct in_addr *maskp, unsigned int *naddrs)
{
	struct in_addr *addrp;
	char buf[256];
	char *p;
	int i, j, k, n;

	strncpy(buf, name, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if ((p = strrchr(buf, '/')) != NULL) {
		*p = '\0';
		addrp = parse_mask(p + 1);
	} else
		addrp = parse_mask(NULL);
	inaddrcpy(maskp, addrp);

	/* if a null mask is given, the name is ignored, like in "any/0" */
	if (maskp->s_addr == 0L)
		strcpy(buf, "0.0.0.0");

	addrp = *addrpp = parse_hostnetwork(buf, naddrs);
	n = *naddrs;
	for (i = 0, j = 0; i < n; i++) {
		addrp[j++].s_addr &= maskp->s_addr;
		for (k = 0; k < j - 1; k++) {
			if (addrp[k].s_addr == addrp[j - 1].s_addr) {
				(*naddrs)--;
				j--;
				break;
			}
		}
	}
}

struct arptables_match *
find_match(const char *name, enum arpt_tryload tryload)
{
	struct arptables_match *ptr;

	for (ptr = arptables_matches; ptr; ptr = ptr->next) {
		if (strcmp(name, ptr->name) == 0)
			break;
	}

	if (ptr && !ptr->loaded) {
		if (tryload != DONT_LOAD)
			ptr->loaded = 1;
		else
			ptr = NULL;
	}
	if(!ptr && (tryload == LOAD_MUST_SUCCEED)) {
		exit_error(PARAMETER_PROBLEM,
			   "Couldn't find match `%s'\n", name);
	}

	if (ptr)
		ptr->used = 1;

	return ptr;
}

static void
parse_interface(const char *arg, char *vianame, unsigned char *mask)
{
	int vialen = strlen(arg);
	unsigned int i;

	memset(mask, 0, IFNAMSIZ);
	memset(vianame, 0, IFNAMSIZ);

	if (vialen + 1 > IFNAMSIZ)
		exit_error(PARAMETER_PROBLEM,
			   "interface name `%s' must be shorter than IFNAMSIZ"
			   " (%i)", arg, IFNAMSIZ-1);

	strcpy(vianame, arg);
	if (vialen == 0)
		memset(mask, 0, IFNAMSIZ);
	else if (vianame[vialen - 1] == '+') {
		memset(mask, 0xFF, vialen - 1);
		memset(mask + vialen - 1, 0, IFNAMSIZ - vialen + 1);
		/* Don't remove `+' here! -HW */
	} else {
		/* Include nul-terminator in match */
		memset(mask, 0xFF, vialen + 1);
		memset(mask + vialen + 1, 0, IFNAMSIZ - vialen - 1);
		for (i = 0; vianame[i]; i++) {
			if (!isalnum(vianame[i]) 
			    && vianame[i] != '_' 
			    && vianame[i] != '.') {
				printf("Warning: weird character in interface"
				       " `%s' (No aliases, :, ! or *).\n",
				       vianame);
				break;
			}
		}
	}
}

/* Can't be zero. */
static int
parse_rulenumber(const char *rule)
{
	unsigned int rulenum;

	if (string_to_number(rule, 1, INT_MAX, &rulenum) == -1)
		exit_error(PARAMETER_PROBLEM,
			   "Invalid rule number `%s'", rule);

	return rulenum;
}

static const char *
parse_target(const char *targetname)
{
	const char *ptr;

	if (strlen(targetname) < 1)
		exit_error(PARAMETER_PROBLEM,
			   "Invalid target name (too short)");

	if (strlen(targetname)+1 > sizeof(arpt_chainlabel))
		exit_error(PARAMETER_PROBLEM,
			   "Invalid target name `%s' (%zu chars max)",
			   targetname, sizeof(arpt_chainlabel)-1);

	for (ptr = targetname; *ptr; ptr++)
		if (isspace(*ptr))
			exit_error(PARAMETER_PROBLEM,
				   "Invalid target name `%s'", targetname);
	return targetname;
}

static char *
addr_to_network(const struct in_addr *addr)
{
	struct netent *net;

	if ((net = getnetbyaddr((long) ntohl(addr->s_addr), AF_INET)) != NULL)
		return (char *) net->n_name;

	return (char *) NULL;
}

char *
addr_to_dotted(const struct in_addr *addrp)
{
	static char buf[20];
	const unsigned char *bytep;

	bytep = (const unsigned char *) &(addrp->s_addr);
	sprintf(buf, "%d.%d.%d.%d", bytep[0], bytep[1], bytep[2], bytep[3]);
	return buf;
}

char *
addr_to_anyname(const struct in_addr *addr)
{
	char *name;

	if ((name = addr_to_host(addr)) != NULL ||
	    (name = addr_to_network(addr)) != NULL)
		return name;

	return addr_to_dotted(addr);
}

char *
mask_to_dotted(const struct in_addr *mask)
{
	int i;
	static char buf[20];
	uint32_t maskaddr, bits;

	maskaddr = ntohl(mask->s_addr);

	if (maskaddr == 0xFFFFFFFFL)
		/* we don't want to see "/32" */
		return "";

	i = 32;
	bits = 0xFFFFFFFEL;
	while (--i >= 0 && maskaddr != bits)
		bits <<= 1;
	if (i >= 0)
		sprintf(buf, "/%d", i);
	else
		/* mask was not a decent combination of 1's and 0's */
		sprintf(buf, "/%s", addr_to_dotted(mask));

	return buf;
}

int
string_to_number(const char *s, unsigned int min, unsigned int max,
		 unsigned int *ret)
{
	long number;
	char *end;

	/* Handle hex, octal, etc. */
	errno = 0;
	number = strtol(s, &end, 0);
	if (*end == '\0' && end != s) {
		/* we parsed a number, let's see if we want this */
		if (errno != ERANGE && min <= number && number <= max) {
			*ret = number;
			return 0;
		}
	}
	return -1;
}

static void
set_option(unsigned int *options, unsigned int option, uint16_t *invflg,
	   int invert)
{
	if (*options & option)
		exit_error(PARAMETER_PROBLEM, "multiple -%c flags not allowed",
			   opt2char(option));
	*options |= option;

	if (invert) {
		unsigned int i;
		for (i = 0; 1 << i != option; i++);

		if (!inverse_for_options[i])
			exit_error(PARAMETER_PROBLEM,
				   "cannot have ! before -%c",
				   opt2char(option));
		*invflg |= inverse_for_options[i];
	}
}

struct arptables_target *
find_target(const char *name, enum arpt_tryload tryload)
{
	struct arptables_target *ptr;

	/* Standard target? */
	if (strcmp(name, "") == 0
	    || strcmp(name, ARPTC_LABEL_ACCEPT) == 0
	    || strcmp(name, ARPTC_LABEL_DROP) == 0
	    || strcmp(name, ARPTC_LABEL_QUEUE) == 0
	    || strcmp(name, ARPTC_LABEL_RETURN) == 0)
		name = "standard";

	for (ptr = arptables_targets; ptr; ptr = ptr->next) {
		if (strcmp(name, ptr->name) == 0)
			break;
	}

	if (ptr && !ptr->loaded) {
		if (tryload != DONT_LOAD)
			ptr->loaded = 1;
		else
			ptr = NULL;
	}
	if(!ptr && (tryload == LOAD_MUST_SUCCEED)) {
		exit_error(PARAMETER_PROBLEM,
			   "Couldn't find target `%s'\n", name);
	}

	if (ptr)
		ptr->used = 1;

	return ptr;
}

static struct option *
merge_options(struct option *oldopts, const struct option *newopts,
	      unsigned int *option_offset)
{
	unsigned int num_old, num_new, i;
	struct option *merge;

	for (num_old = 0; oldopts[num_old].name; num_old++);
	for (num_new = 0; newopts[num_new].name; num_new++);

	global_option_offset += OPTION_OFFSET;
	*option_offset = global_option_offset;

	merge = malloc(sizeof(struct option) * (num_new + num_old + 1));
	memcpy(merge, oldopts, num_old * sizeof(struct option));
	for (i = 0; i < num_new; i++) {
		merge[num_old + i] = newopts[i];
		merge[num_old + i].val += *option_offset;
	}
	memset(merge + num_old + num_new, 0, sizeof(struct option));

	return merge;
}

void
register_match(struct arptables_match *me)
{
	struct arptables_match **i;

	if (strcmp(me->version, program_version) != 0) {
		fprintf(stderr, "%s: match `%s' v%s (I'm v%s).\n",
			program_name, me->name, me->version, program_version);
		exit(1);
	}

	if (find_match(me->name, DONT_LOAD)) {
		fprintf(stderr, "%s: match `%s' already registered.\n",
			program_name, me->name);
		exit(1);
	}

	if (me->size != ARPT_ALIGN(me->size)) {
		fprintf(stderr, "%s: match `%s' has invalid size %zu.\n",
			program_name, me->name, me->size);
		exit(1);
	}

	/* Append to list. */
	for (i = &arptables_matches; *i; i = &(*i)->next);
	me->next = NULL;
	*i = me;

	me->m = NULL;
	me->mflags = 0;
}

void
register_target(struct arptables_target *me)
{
	if (strcmp(me->version, program_version) != 0) {
		fprintf(stderr, "%s: target `%s' v%s (I'm v%s).\n",
			program_name, me->name, me->version, program_version);
		exit(1);
	}

	if (find_target(me->name, DONT_LOAD)) {
		fprintf(stderr, "%s: target `%s' already registered.\n",
			program_name, me->name);
		exit(1);
	}

	if (me->size != ARPT_ALIGN(me->size)) {
		fprintf(stderr, "%s: target `%s' has invalid size %zu.\n",
			program_name, me->name, me->size);
		exit(1);
	}

	/* Prepend to list. */
	me->next = arptables_targets;
	arptables_targets = me;
	me->t = NULL;
	me->tflags = 0;
}

static void
print_num(uint64_t number, unsigned int format)
{
	if (format & FMT_KILOMEGAGIGA) {
		if (number > 99999) {
			number = (number + 500) / 1000;
			if (number > 9999) {
				number = (number + 500) / 1000;
				if (number > 9999) {
					number = (number + 500) / 1000;
					if (number > 9999) {
						number = (number + 500) / 1000;
						printf(FMT("%4"PRIu64"T ","%"PRIu64"T "), number);
					}
					else printf(FMT("%4"PRIu64"G ","%"PRIu64"G "), number);
				}
				else printf(FMT("%4"PRIu64"M ","%"PRIu64"M "), number);
			} else
				printf(FMT("%4"PRIu64"K ","%"PRIu64"K "), number);
		} else
			printf(FMT("%5"PRIu64" ","%"PRIu64" "), number);
	} else
		printf(FMT("%8"PRIu64" ","%"PRIu64" "), number);
}


static void
print_header(unsigned int format, const char *chain, arptc_handle_t *handle)
{
	struct arpt_counters counters;
	const char *pol = arptc_get_policy(chain, &counters, handle);
	printf("Chain %s", chain);
	if (pol) {
		printf(" (policy %s", pol);
		if (!(format & FMT_NOCOUNTS)) {
			fputc(' ', stdout);
			print_num(counters.pcnt, (format|FMT_NOTABLE));
			fputs("packets, ", stdout);
			print_num(counters.bcnt, (format|FMT_NOTABLE));
			fputs("bytes", stdout);
		}
		printf(")\n");
	} else {
		unsigned int refs;
		if (!arptc_get_references(&refs, chain, handle))
			printf(" (ERROR obtaining refs)\n");
		else
			printf(" (%u references)\n", refs);
	}

/* I don't like this
	if (format & FMT_LINENUMBERS)
		printf(FMT("%-4s ", "%s "), "num");
	if (!(format & FMT_NOCOUNTS)) {
		if (format & FMT_KILOMEGAGIGA) {
			printf(FMT("%5s ","%s "), "pkts");
			printf(FMT("%5s ","%s "), "bytes");
		} else {
			printf(FMT("%8s ","%s "), "pkts");
			printf(FMT("%10s ","%s "), "bytes");
		}
	}
	if (!(format & FMT_NOTARGET))
		printf(FMT("%-9s ","%s "), "target");
	fputs(" prot ", stdout);
	if (format & FMT_OPTIONS)
		fputs("opt", stdout);
	if (format & FMT_VIA) {
		printf(FMT(" %-6s ","%s "), "in");
		printf(FMT("%-6s ","%s "), "out");
	}
	printf(FMT(" %-19s ","%s "), "source");
	printf(FMT(" %-19s "," %s "), "destination");
	printf("\n");
*/
}

/*
static int
print_match(const struct arpt_entry_match *m,
	    const struct arpt_arp *arp,
	    int numeric)
{
	struct arptables_match *match = find_match(m->u.user.name, TRY_LOAD);

	if (match) {
		if (match->print)
			match->print(arp, m, numeric);
		else
			printf("%s ", match->name);
	} else {
		if (m->u.user.name[0])
			printf("UNKNOWN match `%s' ", m->u.user.name);
	}
*/
	/* Don't stop iterating. */
/*	return 0;
}
*/

/* e is called `fw' here for hysterical raisins */
static void
print_firewall(const struct arpt_entry *fw,
	       const char *targname,
	       unsigned int num,
	       unsigned int format,
	       const arptc_handle_t handle)
{
	struct arptables_target *target = NULL;
	const struct arpt_entry_target *t;
	char buf[BUFSIZ];
	int i;
	char iface[IFNAMSIZ+2];
	int print_iface = 0;

	if (!arptc_is_chain(targname, handle))
		target = find_target(targname, TRY_LOAD);
	else
		target = find_target(ARPT_STANDARD_TARGET, LOAD_MUST_SUCCEED);

	t = arpt_get_target((struct arpt_entry *)fw);

	if (format & FMT_LINENUMBERS)
		printf("%u ", num+1);

	if (!(format & FMT_NOTARGET) && targname[0] != '\0')
		printf("-j %s ", targname);

	iface[0] = '\0';

	if (fw->arp.iniface[0] != '\0') {
		strcat(iface, fw->arp.iniface);
		print_iface = 1;
	}
	else if (format & FMT_VIA) {
		print_iface = 1;
		if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
	}
	if (print_iface)
		printf("%s-i %s ", fw->arp.invflags & ARPT_INV_VIA_IN ? "! ": "", iface);

	print_iface = 0;
	iface[0] = '\0';

	if (fw->arp.outiface[0] != '\0') {
		strcat(iface, fw->arp.outiface);
		print_iface = 1;
	}
	else if (format & FMT_VIA) {
		print_iface = 1;
		if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
	}
	if (print_iface)
		printf("%s-o %s ", fw->arp.invflags & ARPT_INV_VIA_OUT ? "! " : "", iface);

	if (fw->arp.smsk.s_addr != 0L) {
		printf("%s", fw->arp.invflags & ARPT_INV_SRCIP
			? "! " : "");
		if (format & FMT_NUMERIC)
			sprintf(buf, "%s", addr_to_dotted(&(fw->arp.src)));
		else
			sprintf(buf, "%s", addr_to_anyname(&(fw->arp.src)));
		strncat(buf, mask_to_dotted(&(fw->arp.smsk)), sizeof(buf) - strlen(buf) -1);
		printf("-s %s ", buf);
	}

	for (i = 0; i < ARPT_DEV_ADDR_LEN_MAX; i++)
		if (fw->arp.src_devaddr.mask[i] != 0)
			break;
	if (i == ARPT_DEV_ADDR_LEN_MAX)
		goto after_devsrc;
	printf("%s", fw->arp.invflags & ARPT_INV_SRCDEVADDR
		? "! " : "");
	printf("--src-mac ");
	print_mac_and_mask((unsigned char *)fw->arp.src_devaddr.addr,
		(unsigned char *)fw->arp.src_devaddr.mask, ETH_ALEN);
	printf(" ");
after_devsrc:

	if (fw->arp.tmsk.s_addr != 0L) {
		printf("%s",fw->arp.invflags & ARPT_INV_TGTIP
			? "! " : "");
		if (format & FMT_NUMERIC)
			sprintf(buf, "%s", addr_to_dotted(&(fw->arp.tgt)));
		else
			sprintf(buf, "%s", addr_to_anyname(&(fw->arp.tgt)));
		strncat(buf, mask_to_dotted(&(fw->arp.tmsk)),  sizeof(buf) - strlen(buf) -1);
		printf("-d %s ", buf);
	}

	for (i = 0; i <ARPT_DEV_ADDR_LEN_MAX; i++)
		if (fw->arp.tgt_devaddr.mask[i] != 0)
			break;
	if (i == ARPT_DEV_ADDR_LEN_MAX)
		goto after_devdst;
	printf("%s",fw->arp.invflags & ARPT_INV_TGTDEVADDR
		? "! " : "");
	printf("--dst-mac ");
	print_mac_and_mask((unsigned char *)fw->arp.tgt_devaddr.addr,
		(unsigned char *)fw->arp.tgt_devaddr.mask, ETH_ALEN);
	printf(" ");
after_devdst:

	if (fw->arp.arhln_mask != 0) {
		printf("%s",fw->arp.invflags & ARPT_INV_ARPHLN
			? "! " : "");
		printf("--h-length %d", fw->arp.arhln);
		if (fw->arp.arhln_mask != 255)
			printf("/%d", fw->arp.arhln_mask);
		printf(" ");
	}

	if (fw->arp.arpop_mask != 0) {
		int tmp = ntohs(fw->arp.arpop);

		printf("%s",fw->arp.invflags & ARPT_INV_ARPOP
			? "! " : "");
		if (tmp <= NUMOPCODES && !(format & FMT_NUMERIC))
			printf("--opcode %s", opcodes[tmp-1]);
		else
			printf("--opcode %d", tmp);
		if (fw->arp.arpop_mask != 65535)
			printf("/%d", ntohs(fw->arp.arpop_mask));
		printf(" ");
	}

	if (fw->arp.arhrd_mask != 0) {
		uint16_t tmp = ntohs(fw->arp.arhrd);

		printf("%s", fw->arp.invflags & ARPT_INV_ARPHRD
			? "! " : "");
		if (tmp == 1 && !(format & FMT_NUMERIC))
			printf("--h-type %s", "Ethernet");
		else
			printf("--h-type %u", tmp);
		if (fw->arp.arhrd_mask != 65535)
			printf("/%d", ntohs(fw->arp.arhrd_mask));
		printf(" ");
	}

	if (fw->arp.arpro_mask != 0) {
		int tmp = ntohs(fw->arp.arpro);

		printf("%s", fw->arp.invflags & ARPT_INV_ARPPRO
			? "! " : "");
		if (tmp == 0x0800 && !(format & FMT_NUMERIC))
			printf("--proto-type %s", "IPv4");
		else
			printf("--proto-type 0x%x", tmp);
		if (fw->arp.arpro_mask != 65535)
			printf("/%x", ntohs(fw->arp.arpro_mask));
		printf(" ");
	}

/* FIXME
	ARPT_MATCH_ITERATE(fw, print_match, &fw->ip, format & FMT_NUMERIC);
*/

	if (target) {
		if (target->print)
			/* Print the target information. */
			target->print(&fw->arp, t, format & FMT_NUMERIC);
	} else if (t->u.target_size != sizeof(*t))
		printf("[%zu bytes of unknown target data] ",
		       t->u.target_size - sizeof(*t));

	if (!(format & FMT_NOCOUNTS)) {
		printf(", pcnt=");
		print_num(fw->counters.pcnt, format);
		printf("-- bcnt=");
		print_num(fw->counters.bcnt, format);
	}

	if (!(format & FMT_NONEWLINE))
		fputc('\n', stdout);
}

static void
print_firewall_line(const struct arpt_entry *fw,
		    const arptc_handle_t h)
{
	struct arpt_entry_target *t;

	t = arpt_get_target((struct arpt_entry *)fw);
	print_firewall(fw, t->u.user.name, 0, FMT_PRINT_RULE, h);
}

static int
append_entry(const arpt_chainlabel chain,
	     struct arpt_entry *fw,
	     unsigned int nsaddrs,
	     const struct in_addr saddrs[],
	     unsigned int ndaddrs,
	     const struct in_addr daddrs[],
	     int verbose,
	     arptc_handle_t *handle)
{
	unsigned int i, j;
	int ret = 1;

	for (i = 0; i < nsaddrs; i++) {
		fw->arp.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			fw->arp.tgt.s_addr = daddrs[j].s_addr;
			if (verbose)
				print_firewall_line(fw, *handle);
			ret &= arptc_append_entry(chain, fw, handle);
		}
	}

	return ret;
}

static int
replace_entry(const arpt_chainlabel chain,
	      struct arpt_entry *fw,
	      unsigned int rulenum,
	      const struct in_addr *saddr,
	      const struct in_addr *daddr,
	      int verbose,
	      arptc_handle_t *handle)
{
	fw->arp.src.s_addr = saddr->s_addr;
	fw->arp.tgt.s_addr = daddr->s_addr;

	if (verbose)
		print_firewall_line(fw, *handle);
	return arptc_replace_entry(chain, fw, rulenum, handle);
}

static int
insert_entry(const arpt_chainlabel chain,
	     struct arpt_entry *fw,
	     unsigned int rulenum,
	     unsigned int nsaddrs,
	     const struct in_addr saddrs[],
	     unsigned int ndaddrs,
	     const struct in_addr daddrs[],
	     int verbose,
	     arptc_handle_t *handle)
{
	unsigned int i, j;
	int ret = 1;

	for (i = 0; i < nsaddrs; i++) {
		fw->arp.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			fw->arp.tgt.s_addr = daddrs[j].s_addr;
			if (verbose)
				print_firewall_line(fw, *handle);
			ret &= arptc_insert_entry(chain, fw, rulenum, handle);
		}
	}

	return ret;
}

static unsigned char *
make_delete_mask(struct arpt_entry *fw)
{
	/* Establish mask for comparison */
	unsigned int size;
	struct arptables_match *m;
	unsigned char *mask, *mptr;

	size = sizeof(struct arpt_entry);
	for (m = arptables_matches; m; m = m->next) {
		if (!m->used)
			continue;

		size += ARPT_ALIGN(sizeof(struct arpt_entry_match)) + m->size;
	}

	mask = fw_calloc(1, size
			 + ARPT_ALIGN(sizeof(struct arpt_entry_target))
			 + arptables_targets->size);

	memset(mask, 0xFF, sizeof(struct arpt_entry));
	mptr = mask + sizeof(struct arpt_entry);

	for (m = arptables_matches; m; m = m->next) {
		if (!m->used)
			continue;

		memset(mptr, 0xFF,
		       ARPT_ALIGN(sizeof(struct arpt_entry_match))
		       + m->userspacesize);
		mptr += ARPT_ALIGN(sizeof(struct arpt_entry_match)) + m->size;
	}

	memset(mptr, 0xFF,
	       ARPT_ALIGN(sizeof(struct arpt_entry_target))
	       + arptables_targets->userspacesize);

	return mask;
}

static int
delete_entry(const arpt_chainlabel chain,
	     struct arpt_entry *fw,
	     unsigned int nsaddrs,
	     const struct in_addr saddrs[],
	     unsigned int ndaddrs,
	     const struct in_addr daddrs[],
	     int verbose,
	     arptc_handle_t *handle)
{
	unsigned int i, j;
	int ret = 1;
	unsigned char *mask;

	mask = make_delete_mask(fw);
	for (i = 0; i < nsaddrs; i++) {
		fw->arp.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			fw->arp.tgt.s_addr = daddrs[j].s_addr;
			if (verbose)
				print_firewall_line(fw, *handle);
			ret &= arptc_delete_entry(chain, fw, mask, handle);
		}
	}
	return ret;
}

int
for_each_chain(int (*fn)(const arpt_chainlabel, int, arptc_handle_t *),
	       int verbose, int builtinstoo, arptc_handle_t *handle)
{
        int ret = 1;
	const char *chain;
	char *chains;
	unsigned int i, chaincount = 0;

	chain = arptc_first_chain(handle);
	while (chain) {
		chaincount++;
		chain = arptc_next_chain(handle);
        }

	chains = fw_malloc(sizeof(arpt_chainlabel) * chaincount);
	i = 0;
	chain = arptc_first_chain(handle);
	while (chain) {
		strcpy(chains + i*sizeof(arpt_chainlabel), chain);
		i++;
		chain = arptc_next_chain(handle);
        }

	for (i = 0; i < chaincount; i++) {
		if (!builtinstoo
		    && arptc_builtin(chains + i*sizeof(arpt_chainlabel),
				    *handle))
			continue;
	        ret &= fn(chains + i*sizeof(arpt_chainlabel), verbose, handle);
	}

	free(chains);
        return ret;
}

int
flush_entries(const arpt_chainlabel chain, int verbose,
	      arptc_handle_t *handle)
{
	if (!chain)
		return for_each_chain(flush_entries, verbose, 1, handle);

	if (verbose)
		fprintf(stdout, "Flushing chain `%s'\n", chain);
	return arptc_flush_entries(chain, handle);
}

static int
zero_entries(const arpt_chainlabel chain, int verbose,
	     arptc_handle_t *handle)
{
	if (!chain)
		return for_each_chain(zero_entries, verbose, 1, handle);

	if (verbose)
		fprintf(stdout, "Zeroing chain `%s'\n", chain);
	return arptc_zero_entries(chain, handle);
}

int
delete_chain(const arpt_chainlabel chain, int verbose,
	     arptc_handle_t *handle)
{
	if (!chain)
		return for_each_chain(delete_chain, verbose, 0, handle);

	if (verbose)
	        fprintf(stdout, "Deleting chain `%s'\n", chain);
	return arptc_delete_chain(chain, handle);
}

static int
list_entries(const arpt_chainlabel chain, int verbose, int numeric,
	     int expanded, int linenumbers, arptc_handle_t *handle)
{
	int found = 0;
	unsigned int format;
	const char *this;

	format = FMT_OPTIONS;
	if (!verbose)
		format |= FMT_NOCOUNTS;
	else
		format |= FMT_VIA;

	if (numeric)
		format |= FMT_NUMERIC;

	if (!expanded)
		format |= FMT_KILOMEGAGIGA;

	if (linenumbers)
		format |= FMT_LINENUMBERS;

	for (this = arptc_first_chain(handle);
	     this;
	     this = arptc_next_chain(handle)) {
		const struct arpt_entry *i;
		unsigned int num;

		if (chain && strcmp(chain, this) != 0)
			continue;

		if (found) printf("\n");

		print_header(format, this, handle);
		i = arptc_first_rule(this, handle);

		num = 0;
		while (i) {
			print_firewall(i,
				       arptc_get_target(i, handle),
				       num++,
				       format,
				       *handle);
			i = arptc_next_rule(i, handle);
		}
		found = 1;
	}

	errno = ENOENT;
	return found;
}

static char *get_modprobe(void)
{
	int procfile;
	char *ret;

	procfile = open(PROC_SYS_MODPROBE, O_RDONLY);
	if (procfile < 0)
		return NULL;

	ret = malloc(1024);
	if (ret) {
		int read_bytes = read(procfile, ret, 1024);
		switch (read_bytes) {
		case -1: goto fail;
		case 1024: goto fail; /* Partial read.  Wierd */
		}
		ret[read_bytes] = '\0';
		if (ret[strlen(ret)-1]=='\n')
			ret[strlen(ret)-1]=0;
		close(procfile);
		return ret;
	}
 fail:
	free(ret);
	close(procfile);
	return NULL;
}

int arptables_insmod(const char *modname, const char *modprobe)
{
	char *buf = NULL;
	char *argv[3];

	/* If they don't explicitly set it, read out of kernel */
	if (!modprobe) {
		buf = get_modprobe();
		if (!buf)
			return -1;
		modprobe = buf;
	}

	switch (fork()) {
	case 0:
		argv[0] = (char *)modprobe;
		argv[1] = (char *)modname;
		argv[2] = NULL;
		execv(argv[0], argv);

		/* not usually reached */
		exit(0);
	case -1:
		return -1;

	default: /* parent */
		wait(NULL);
	}

	free(buf);
	return 0;
}

static struct arpt_entry *
generate_entry(const struct arpt_entry *fw,
	       struct arptables_match *matches,
	       struct arpt_entry_target *target)
{
	unsigned int size;
	/*
	struct arptables_match *m;
	*/
	struct arpt_entry *e;

	size = sizeof(struct arpt_entry);
	/* FIXME
	for (m = matches; m; m = m->next) {
		if (!m->used)
			continue;

		size += m->m->u.match_size;
	}
	*/

	e = fw_malloc(size + target->u.target_size);
	*e = *fw;
	e->target_offset = size;
	e->next_offset = size + target->u.target_size;

	size = 0;
	/* FIXME
	for (m = matches; m; m = m->next) {
		if (!m->used)
			continue;

		memcpy(e->elems + size, m->m, m->m->u.match_size);
		size += m->m->u.match_size;
	}
	*/

	memcpy(e->elems + size, target, target->u.target_size);

	return e;
}

int do_command(int argc, char *argv[], char **table, arptc_handle_t *handle)
{
	struct arpt_entry fw, *e = NULL;
	int invert = 0;
	unsigned int nsaddrs = 0, ndaddrs = 0;
	struct in_addr *saddrs = NULL, *daddrs = NULL;

	int c, verbose = 0;
	const char *chain = NULL;
	const char *shostnetworkmask = NULL, *dhostnetworkmask = NULL;
	const char *policy = NULL, *newname = NULL;
	unsigned int rulenum = 0, options = 0, command = 0;
	const char *pcnt = NULL, *bcnt = NULL;
	int ret = 1;
/*	struct arptables_match *m;*/
	struct arptables_target *target = NULL;
	struct arptables_target *t;
	const char *jumpto = "";
	char *protocol = NULL;
	const char *modprobe = NULL;

	/* first figure out if this is a 2.6 or a 2.4 kernel */
	*handle = arptc_init(*table);

	if (!*handle) {
		arptables_insmod("arp_tables", modprobe);
		*handle = arptc_init(*table);
		if (!*handle) {
			RUNTIME_NF_ARP_NUMHOOKS = 2;
			*handle = arptc_init(*table);
			if (!*handle) {
				exit_error(VERSION_PROBLEM,
				"can't initialize arptables table `%s': %s",
				*table, arptc_strerror(errno));
			}
		}
	}

	memset(&fw, 0, sizeof(fw));
	opts = original_opts;
	global_option_offset = 0;

	/* re-set optind to 0 in case do_command gets called
	 * a second time */
	optind = 0;

	/* clear mflags in case do_command gets called a second time
	 * (we clear the global list of all matches for security)*/
/*	for (m = arptables_matches; m; m = m->next) {
		m->mflags = 0;
		m->used = 0;
	}*/

	for (t = arptables_targets; t; t = t->next) {
		t->tflags = 0;
		t->used = 0;
	}

	/* Suppress error messages: we may add new options if we
           demand-load a protocol. */
	opterr = 0;

	while ((c = getopt_long(argc, argv,
	   "-A:D:R:I:L::M:F::Z::N:X::E:P:Vh::o:p:s:d:j:l:i:vnt:m:c:",
					   opts, NULL)) != -1) {
		switch (c) {
			/*
			 * Command selection
			 */
		case 'A':
			add_command(&command, CMD_APPEND, CMD_NONE,
				    invert);
			chain = optarg;
			break;

		case 'D':
			add_command(&command, CMD_DELETE, CMD_NONE,
				    invert);
			chain = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!') {
				rulenum = parse_rulenumber(argv[optind++]);
				command = CMD_DELETE_NUM;
			}
			break;

		case 'R':
			add_command(&command, CMD_REPLACE, CMD_NONE,
				    invert);
			chain = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!')
				rulenum = parse_rulenumber(argv[optind++]);
			else
				exit_error(PARAMETER_PROBLEM,
					   "-%c requires a rule number",
					   cmd2char(CMD_REPLACE));
			break;

		case 'I':
			add_command(&command, CMD_INSERT, CMD_NONE,
				    invert);
			chain = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!')
				rulenum = parse_rulenumber(argv[optind++]);
			else rulenum = 1;
			break;

		case 'L':
			add_command(&command, CMD_LIST, CMD_ZERO,
				    invert);
			if (optarg) chain = optarg;
			else if (optind < argc && argv[optind][0] != '-'
				 && argv[optind][0] != '!')
				chain = argv[optind++];
			break;

		case 'F':
			add_command(&command, CMD_FLUSH, CMD_NONE,
				    invert);
			if (optarg) chain = optarg;
			else if (optind < argc && argv[optind][0] != '-'
				 && argv[optind][0] != '!')
				chain = argv[optind++];
			break;

		case 'Z':
			add_command(&command, CMD_ZERO, CMD_LIST,
				    invert);
			if (optarg) chain = optarg;
			else if (optind < argc && argv[optind][0] != '-'
				&& argv[optind][0] != '!')
				chain = argv[optind++];
			break;

		case 'N':
			if (optarg && *optarg == '-')
				exit_error(PARAMETER_PROBLEM,
					   "chain name not allowed to start "
					   "with `-'\n");
			if (find_target(optarg, TRY_LOAD))
				exit_error(PARAMETER_PROBLEM,
					   "chain name may not clash "
					   "with target name\n");
			add_command(&command, CMD_NEW_CHAIN, CMD_NONE,
				    invert);
			chain = optarg;
			break;

		case 'X':
			add_command(&command, CMD_DELETE_CHAIN, CMD_NONE,
				    invert);
			if (optarg) chain = optarg;
			else if (optind < argc && argv[optind][0] != '-'
				 && argv[optind][0] != '!')
				chain = argv[optind++];
			break;

		case 'E':
			add_command(&command, CMD_RENAME_CHAIN, CMD_NONE,
				    invert);
			chain = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!')
				newname = argv[optind++];
			else
				exit_error(PARAMETER_PROBLEM,
				           "-%c requires old-chain-name and "
					   "new-chain-name",
					    cmd2char(CMD_RENAME_CHAIN));
			break;

		case 'P':
			add_command(&command, CMD_SET_POLICY, CMD_NONE,
				    invert);
			chain = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!')
				policy = argv[optind++];
			else
				exit_error(PARAMETER_PROBLEM,
					   "-%c requires a chain and a policy",
					   cmd2char(CMD_SET_POLICY));
			break;

		case 'h':
			if (!optarg)
				optarg = argv[optind];

			/* arptables -p icmp -h */
			if (!arptables_matches && protocol)
				find_match(protocol, TRY_LOAD);

			exit_printhelp();

		case 's':
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_S_IP, &fw.arp.invflags,
				   invert);
			shostnetworkmask = argv[optind-1];
			break;

		case 'd':
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_D_IP, &fw.arp.invflags,
				   invert);
			dhostnetworkmask = argv[optind-1];
			break;

		case 2:/* src-mac */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_S_MAC, &fw.arp.invflags,
				   invert);
			if (getmac_and_mask(argv[optind - 1],
			   fw.arp.src_devaddr.addr, fw.arp.src_devaddr.mask))
				exit_error(PARAMETER_PROBLEM, "Problem with specified "
				            "source mac");
			break;

		case 3:/* dst-mac */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_D_MAC, &fw.arp.invflags,
				   invert);

			if (getmac_and_mask(argv[optind - 1],
			   fw.arp.tgt_devaddr.addr, fw.arp.tgt_devaddr.mask))
				exit_error(PARAMETER_PROBLEM, "Problem with specified "
				            "destination mac");
			break;

		case 'l':/* hardware length */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_H_LENGTH, &fw.arp.invflags,
				   invert);
			getlength_and_mask(argv[optind - 1], &fw.arp.arhln,
					   &fw.arp.arhln_mask);
			break;

		case 8:/* protocol length */
		exit_error(PARAMETER_PROBLEM, "not supported");
/*
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_P_LENGTH, &fw.arp.invflags,
				   invert);

			getlength_and_mask(argv[optind - 1], &fw.arp.arpln,
					   &fw.arp.arpln_mask);
			break;
*/

		case 4:/* opcode */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_OPCODE, &fw.arp.invflags,
				   invert);
			if (get16_and_mask(argv[optind - 1], &fw.arp.arpop, &fw.arp.arpop_mask, 10)) {
				int i;

				for (i = 0; i < NUMOPCODES; i++)
					if (!strcasecmp(opcodes[i], optarg))
						break;
				if (i == NUMOPCODES)
					exit_error(PARAMETER_PROBLEM, "Problem with specified opcode");
				fw.arp.arpop = htons(i+1);
			}
			break;

		case 5:/* h-type */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_H_TYPE, &fw.arp.invflags,
				   invert);
			if (get16_and_mask(argv[optind - 1], &fw.arp.arhrd, &fw.arp.arhrd_mask, 16)) {
				if (strcasecmp(argv[optind-1], "Ethernet"))
					exit_error(PARAMETER_PROBLEM, "Problem with specified hardware type");
				fw.arp.arhrd = htons(1);
			}
			break;

		case 6:/* proto-type */
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_P_TYPE, &fw.arp.invflags,
				   invert);
			if (get16_and_mask(argv[optind - 1], &fw.arp.arpro, &fw.arp.arpro_mask, 0)) {
				if (strcasecmp(argv[optind-1], "ipv4"))
					exit_error(PARAMETER_PROBLEM, "Problem with specified protocol type");
				fw.arp.arpro = htons(0x800);
			}
			break;

		case 'j':
			set_option(&options, OPT_JUMP, &fw.arp.invflags,
				   invert);
			jumpto = parse_target(optarg);
			/* TRY_LOAD (may be chain name) */
			target = find_target(jumpto, TRY_LOAD);

			if (target) {
				size_t size;

				size = ARPT_ALIGN(sizeof(struct arpt_entry_target))
					+ target->size;

				target->t = fw_calloc(1, size);
				target->t->u.target_size = size;
				strncpy(target->t->u.user.name, jumpto, sizeof(target->t->u.user.name) - 1);
				target->t->u.user.revision = target->revision;
/*
				target->init(target->t, &fw.nfcache);
*/
				target->init(target->t);

				opts = merge_options(opts, target->extra_opts, &target->option_offset);
			}
			break;


		case 'i':
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_VIANAMEIN, &fw.arp.invflags,
				   invert);
			parse_interface(argv[optind-1],
					fw.arp.iniface,
					fw.arp.iniface_mask);
/*			fw.nfcache |= NFC_IP_IF_IN; */
			break;

		case 'o':
			check_inverse(optarg, &invert, &optind, argc);
			set_option(&options, OPT_VIANAMEOUT, &fw.arp.invflags,
				   invert);
			parse_interface(argv[optind-1],
					fw.arp.outiface,
					fw.arp.outiface_mask);
			/* fw.nfcache |= NFC_IP_IF_OUT; */
			break;

		case 'v':
			if (!verbose)
				set_option(&options, OPT_VERBOSE,
					   &fw.arp.invflags, invert);
			verbose++;
			break;

		case 'm': /*{
			size_t size;

			if (invert)
				exit_error(PARAMETER_PROBLEM,
					   "unexpected ! flag before --match");

			m = find_match(optarg, LOAD_MUST_SUCCEED);
			size = ARPT_ALIGN(sizeof(struct arpt_entry_match))
					 + m->size;
			m->m = fw_calloc(1, size);
			m->m->u.match_size = size;
			strcpy(m->m->u.user.name, m->name);
			m->init(m->m, &fw.nfcache);
			opts = merge_options(opts, m->extra_opts, &m->option_offset);
		}*/
		break;

		case 'n':
			set_option(&options, OPT_NUMERIC, &fw.arp.invflags,
				   invert);
			break;

		case 't':
			if (invert)
				exit_error(PARAMETER_PROBLEM,
					   "unexpected ! flag before --table");
			*table = argv[optind-1];
			break;

		case 'V':
			if (invert)
				printf("Not %s ;-)\n", program_version);
			else
				printf("%s v%s\n",
				       program_name, program_version);
			exit(0);

		case '0':
			set_option(&options, OPT_LINENUMBERS, &fw.arp.invflags,
				   invert);
			break;

		case 'M':
			modprobe = optarg;
			break;

		case 'c':

			set_option(&options, OPT_COUNTERS, &fw.arp.invflags,
				   invert);
			pcnt = optarg;
			if (optind < argc && argv[optind][0] != '-'
			    && argv[optind][0] != '!')
				bcnt = argv[optind++];
			else
				exit_error(PARAMETER_PROBLEM,
					"-%c requires packet and byte counter",
					opt2char(OPT_COUNTERS));

			if (sscanf(pcnt, "%"PRIu64,
				   (uint64_t *)&fw.counters.pcnt) != 1)
				exit_error(PARAMETER_PROBLEM,
					"-%c packet counter not numeric",
					opt2char(OPT_COUNTERS));

			if (sscanf(bcnt, "%"PRIu64,
				   (uint64_t *)&fw.counters.bcnt) != 1)
				exit_error(PARAMETER_PROBLEM,
					"-%c byte counter not numeric",
					opt2char(OPT_COUNTERS));
			break;


		case 1: /* non option */
			if (optarg[0] == '!' && optarg[1] == '\0') {
				if (invert)
					exit_error(PARAMETER_PROBLEM,
						   "multiple consecutive ! not"
						   " allowed");
				invert = TRUE;
				optarg[0] = '\0';
				continue;
			}
			printf("Bad argument `%s'\n", optarg);
			exit_tryhelp(2);

		default:
			/* FIXME: This scheme doesn't allow two of the same
			   matches --RR */
			if (!target
			    || !(target->parse(c - target->option_offset,
					       argv, invert,
					       &target->tflags,
					       &fw, &target->t))) {
/*
				for (m = arptables_matches; m; m = m->next) {
					if (!m->used)
						continue;

					if (m->parse(c - m->option_offset,
						     argv, invert,
						     &m->mflags,
						     &fw,
						     &fw.nfcache,
						     &m->m))
						break;
				}
*/

				/* If you listen carefully, you can
				   actually hear this code suck. */

				/* some explanations (after four different bugs
				 * in 3 different releases): If we encountere a
				 * parameter, that has not been parsed yet,
				 * it's not an option of an explicitly loaded
				 * match or a target.  However, we support
				 * implicit loading of the protocol match
				 * extension.  '-p tcp' means 'l4 proto 6' and
				 * at the same time 'load tcp protocol match on
				 * demand if we specify --dport'.
				 *
				 * To make this work, we need to make sure:
				 * - the parameter has not been parsed by
				 *   a match (m above)
				 * - a protocol has been specified
				 * - the protocol extension has not been
				 *   loaded yet, or is loaded and unused
				 *   [think of arptables-restore!]
				 * - the protocol extension can be successively
				 *   loaded
				 */
/*
				if (m == NULL
				    && protocol
				    && (!find_proto(protocol, DONT_LOAD,
						   options&OPT_NUMERIC) 
					|| (find_proto(protocol, DONT_LOAD,
							options&OPT_NUMERIC)
					    && (proto_used == 0))
				       )
				    && (m = find_proto(protocol, TRY_LOAD,
						       options&OPT_NUMERIC))) {
					 Try loading protocol */
/*
					size_t size;

					proto_used = 1;

					size = ARPT_ALIGN(sizeof(struct arpt_entry_match))
							 + m->size;

					m->m = fw_calloc(1, size);
					m->m->u.match_size = size;
					strcpy(m->m->u.user.name, m->name);
					m->init(m->m, &fw.nfcache);

					opts = merge_options(opts,
					    m->extra_opts, &m->option_offset);

					optind--;
					continue;
				}
				if (!m)
					exit_error(PARAMETER_PROBLEM,
						   "Unknown arg `%s'",
						   argv[optind-1]);
*/
			}
		}
		invert = FALSE;
	}
/*
	for (m = arptables_matches; m; m = m->next) {
		if (!m->used)
			continue;

		m->final_check(m->mflags);
	}
*/

	if (target)
		target->final_check(target->tflags);

	/* Fix me: must put inverse options checking here --MN */

	if (optind < argc)
		exit_error(PARAMETER_PROBLEM,
			   "unknown arguments found on commandline");
	if (!command)
		exit_error(PARAMETER_PROBLEM, "no command specified");
	if (invert)
		exit_error(PARAMETER_PROBLEM,
			   "nothing appropriate following !");

	if (command & (CMD_REPLACE | CMD_INSERT | CMD_DELETE | CMD_APPEND)) {
		if (!(options & OPT_D_IP))
			dhostnetworkmask = "0.0.0.0/0";
		if (!(options & OPT_S_IP))
			shostnetworkmask = "0.0.0.0/0";
	}

	if (shostnetworkmask)
		parse_hostnetworkmask(shostnetworkmask, &saddrs,
				      &(fw.arp.smsk), &nsaddrs);

	if (dhostnetworkmask)
		parse_hostnetworkmask(dhostnetworkmask, &daddrs,
				      &(fw.arp.tmsk), &ndaddrs);

	if ((nsaddrs > 1 || ndaddrs > 1) &&
	    (fw.arp.invflags & (ARPT_INV_SRCIP | ARPT_INV_TGTIP)))
		exit_error(PARAMETER_PROBLEM, "! not allowed with multiple"
			   " source or destination IP addresses");

	if (command == CMD_REPLACE && (nsaddrs != 1 || ndaddrs != 1))
		exit_error(PARAMETER_PROBLEM, "Replacement rule does not "
			   "specify a unique address");

	generic_opt_check(command, options);

	if (chain && strlen(chain) > ARPT_FUNCTION_MAXNAMELEN)
		exit_error(PARAMETER_PROBLEM,
			   "chain name `%s' too long (must be under %i chars)",
			   chain, ARPT_FUNCTION_MAXNAMELEN);

	/* only allocate handle if we weren't called with a handle */
	if (!*handle)
		*handle = arptc_init(*table);

	if (!*handle) {
		/* try to insmod the module if arptc_init failed */
		arptables_insmod("arp_tables", modprobe);
		*handle = arptc_init(*table);
	}

	if (!*handle)
		exit_error(VERSION_PROBLEM,
			   "can't initialize arptables table `%s': %s",
			   *table, arptc_strerror(errno));

	if (command == CMD_APPEND
	    || command == CMD_DELETE
	    || command == CMD_INSERT
	    || command == CMD_REPLACE) {
		if (strcmp(chain, "PREROUTING") == 0
		    || strcmp(chain, "INPUT") == 0) {
			/* -o not valid with incoming packets. */
			if (options & OPT_VIANAMEOUT)
				exit_error(PARAMETER_PROBLEM,
					   "Can't use -%c with %s\n",
					   opt2char(OPT_VIANAMEOUT),
					   chain);
		}

		if (strcmp(chain, "POSTROUTING") == 0
		    || strcmp(chain, "OUTPUT") == 0) {
			/* -i not valid with outgoing packets */
			if (options & OPT_VIANAMEIN)
				exit_error(PARAMETER_PROBLEM,
					   "Can't use -%c with %s\n",
					   opt2char(OPT_VIANAMEIN),
					   chain);
		}

		if (target && arptc_is_chain(jumpto, *handle)) {
			printf("Warning: using chain %s, not extension\n",
			       jumpto);

			target = NULL;
		}

		/* If they didn't specify a target, or it's a chain
		   name, use standard. */
		if (!target
		    && (strlen(jumpto) == 0
			|| arptc_is_chain(jumpto, *handle))) {
			size_t size;

			target = find_target(ARPT_STANDARD_TARGET,
					     LOAD_MUST_SUCCEED);

			size = sizeof(struct arpt_entry_target)
				+ target->size;
			target->t = fw_calloc(1, size);
			target->t->u.target_size = size;
			strcpy(target->t->u.user.name, jumpto);
			target->t->u.user.revision = target->revision;
			target->init(target->t);
		}

		if (!target) {
			/* it is no chain, and we can't load a plugin.
			 * We cannot know if the plugin is corrupt, non
			 * existant OR if the user just misspelled a
			 * chain. */
			find_target(jumpto, LOAD_MUST_SUCCEED);
		} else {
			e = generate_entry(&fw, arptables_matches, target->t);
		}
	}

	switch (command) {
	case CMD_APPEND:
		ret = append_entry(chain, e,
				   nsaddrs, saddrs, ndaddrs, daddrs,
				   options&OPT_VERBOSE,
				   handle);
		break;
	case CMD_DELETE:
		ret = delete_entry(chain, e,
				   nsaddrs, saddrs, ndaddrs, daddrs,
				   options&OPT_VERBOSE,
				   handle);
		break;
	case CMD_DELETE_NUM:
		ret = arptc_delete_num_entry(chain, rulenum - 1, handle);
		break;
	case CMD_REPLACE:
		ret = replace_entry(chain, e, rulenum - 1,
				    saddrs, daddrs, options&OPT_VERBOSE,
				    handle);
		break;
	case CMD_INSERT:
		ret = insert_entry(chain, e, rulenum - 1,
				   nsaddrs, saddrs, ndaddrs, daddrs,
				   options&OPT_VERBOSE,
				   handle);
		break;
	case CMD_LIST:
		ret = list_entries(chain,
				   options&OPT_VERBOSE,
				   options&OPT_NUMERIC,
				   /*options&OPT_EXPANDED*/0,
				   options&OPT_LINENUMBERS,
				   handle);
		break;
	case CMD_FLUSH:
		ret = flush_entries(chain, options&OPT_VERBOSE, handle);
		break;
	case CMD_ZERO:
		ret = zero_entries(chain, options&OPT_VERBOSE, handle);
		break;
	case CMD_LIST|CMD_ZERO:
		ret = list_entries(chain,
				   options&OPT_VERBOSE,
				   options&OPT_NUMERIC,
				   /*options&OPT_EXPANDED*/0,
				   options&OPT_LINENUMBERS,
				   handle);
		if (ret)
			ret = zero_entries(chain,
					   options&OPT_VERBOSE, handle);
		break;
	case CMD_NEW_CHAIN:
		ret = arptc_create_chain(chain, handle);
		break;
	case CMD_DELETE_CHAIN:
		ret = delete_chain(chain, options&OPT_VERBOSE, handle);
		break;
	case CMD_RENAME_CHAIN:
		ret = arptc_rename_chain(chain, newname,	handle);
		break;
	case CMD_SET_POLICY:
		ret = arptc_set_policy(chain, policy, NULL, handle);
		break;
	default:
		/* We should never reach this... */
		exit_tryhelp(2);
	}

	if (verbose > 1)
		dump_entries(*handle);

	return ret;
}

