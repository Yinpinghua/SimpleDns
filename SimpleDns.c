#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define BUF_SIZE 	1500
#define DOMAINLEN	256

/* These values are taken from RFC1537 */
#define DEFAULT_REFRESH     (60 * 60 * 8)
#define DEFAULT_RETRY       (60 * 60 * 2)
#define DEFAULT_EXPIRE      (60 * 60 * 24 * 7)
#define DEFAULT_MINIMUM     (60 * 60 * 24)


/*
* This software is licensed under the CC0.
*
* This is a _basic_ DNS Server for educational use.
*  It doesn't prevent invalid packets from crashing
*  the server.
*
* To test start the program and issue a DNS request:
*  dig @127.0.0.1 -p 9000 foo.bar.com 
*/


/*
* Masks and constants.
*/

static const uint32_t QR_MASK = 0x8000;
static const uint32_t OPCODE_MASK = 0x7800;
static const uint32_t AA_MASK = 0x0400;
static const uint32_t TC_MASK = 0x0200;
static const uint32_t RD_MASK = 0x0100;
static const uint32_t RA_MASK = 0x8000;
static const uint32_t RCODE_MASK = 0x000F;

/* Response Type */
enum {
	RT_NoError = 0,
	RT_FormErr = 1,
	RT_ServFail = 2,
	RT_NxDomain = 3,
	RT_NotImp = 4,
	RT_Refused = 5,
	RT_YXDomain = 6,
	RT_YXRRSet = 7,
	RT_NXRRSet = 8,
	RT_NotAuth = 9,
	RT_NotZone = 10
};

/* Resource Record Types */
enum {
	RR_A = 1,
	RR_NS = 2,
	RR_CNAME = 5,
	RR_SOA = 6,
	RR_PTR = 12,
	RR_MX = 15,
	RR_TXT = 16,
	RR_AAAA = 28,
	RR_SRV = 33
};

/* Operation Code */
enum {
	QUERY_OperationCode = 0, /* standard query */
	IQUERY_OperationCode = 1, /* inverse query */
	STATUS_OperationCode = 2, /* server status request */
	NOTIFY_OperationCode = 4, /* request zone transfer */
	UPDATE_OperationCode = 5 /* change resource records */
};

/* Response Code */
enum {
	NoError_ResponseCode = 0,
	FormatError_ResponseCode = 1,
	ServerFailure_ResponseCode = 2,
	NameError_ResponseCode = 3
};

/* Query Type */
enum {
	IXFR_QueryType = 251,
	AXFR_QueryType = 252,
	MAILB_QueryType = 253,
	MAILA_QueryType = 254,
	STAR_QueryType = 255
};

/*
* Types.
*/

/* Question Section */
struct Question {
	char *qName;
	uint16_t qType;
	uint16_t qClass;
	struct Question* next; // for linked list
};

/* Data part of a Resource Record */
union ResourceData {
	struct {
		char *txt_data;
	} txt_record;
	struct {
		uint8_t addr[4];
	} a_record;
	struct {
		char* MName;
		char* RName;
		uint32_t serial;
		uint32_t refresh;
		uint32_t retry;
		uint32_t expire;
		uint32_t minimum;
	} soa_record;
	struct {
		char *name;
	} ns_record;
	struct {
		char *name;
	} cname_record;
	struct {
		char *name;
	} ptr_record;
	struct {
		uint16_t preference;
		char *exchange;
	} mx_record;
	struct {
		uint8_t addr[16];
	} aaaa_record;
	struct {
		uint16_t priority;
		uint16_t weight;
		uint16_t port;
		char *target;
	} srv_record;
};

/* Resource Record Section */
struct ResourceRecord {
	char *name;
	uint16_t type;
	uint16_t class;
	uint32_t ttl;
	uint16_t rd_length;
	union ResourceData rd_data;
	struct ResourceRecord* next; // for linked list
};

struct Message {
	uint16_t id; /* Identifier */

	/* Flags */
	uint16_t qr; /* Query/Response Flag */
	uint16_t opcode; /* Operation Code */
	uint16_t aa; /* Authoritative Answer Flag */
	uint16_t tc; /* Truncation Flag */
	uint16_t rd; /* Recursion Desired */
	uint16_t ra; /* Recursion Available */
	uint16_t rcode; /* Response Code */

	uint16_t qdCount; /* Question Count */
	uint16_t anCount; /* Answer Record Count */
	uint16_t nsCount; /* Authority Record Count */
	uint16_t arCount; /* Additional Record Count */

	/* At least one question; questions are copied to the response 1:1 */
	struct Question* questions;

	/*
	* Resource records to be send back.
	* Every resource record can be in any of the following places.
	* But every place has a different semantic.
	*/
	struct ResourceRecord* answers;
	struct ResourceRecord* authorities;
	struct ResourceRecord* additionals;

	struct in_addr cliaddr;
};

int get_A_Record(uint8_t addr[4], const char domain_name[])
{
	if(strcmp("foo.bar.com", domain_name) == 0)
	{
		addr[0] = 192;
		addr[1] = 168;
		addr[2] = 1;
		addr[3] = 1;
		return 0;
	}
	else
	{
		return -1;
	}
}

int get_AAAA_Record(uint8_t addr[16], const char domain_name[])
{
	if(strcmp("foo.bar.com", domain_name) == 0)
	{
		addr[0] = 0xfe;
		addr[1] = 0x80;
		addr[2] = 0x00;
		addr[3] = 0x00;
		addr[4] = 0x00;
		addr[5] = 0x00;
		addr[6] = 0x00;
		addr[7] = 0x00;
		addr[8] = 0x00;
		addr[9] = 0x00;
		addr[10] = 0x00;
		addr[11] = 0x00;
		addr[12] = 0x00;
		addr[13] = 0x00;
		addr[14] = 0x00;
		addr[15] = 0x00;
		return 0;
	}
	else
	{
		return -1;
	}
}


/*
* Debugging functions.
*/

void print_hex(uint8_t* buf, size_t len)
{
	int i;
	printf("%u bytes:\n", len);
	for(i = 0; i < len; ++i)
		printf("%02x ", buf[i]);
	printf("\n");
}

void print_resource_record(struct ResourceRecord* rr)
{
	int i;
	while(rr)
	{
		printf("  ResourceRecord { name '%s', type %u, class %u, ttl %u, rd_length %u, ",
				rr->name,
				rr->type,
				rr->class,
				rr->ttl,
				rr->rd_length
		);

		union ResourceData *rd = &rr->rd_data;
		switch(rr->type)
		{
			case RR_A:
				printf("Address Resource Record { address ");
			
				for(i = 0; i < 4; ++i)
					printf("%s%u", (i ? "." : ""), rd->a_record.addr[i]);
			
				printf(" }");
				break;
			case RR_NS:
				printf("Name Server Resource Record { name %u}",
					rd->ns_record.name
				);
				break;
			case RR_CNAME:
				printf("Canonical Name Resource Record { name %u}",
					rd->cname_record.name
				);
				break;
			case RR_SOA:
				printf("SOA { MName '%s', RName '%s', serial %u, refresh %u, retry %u, expire %u, minimum %u }",
					rd->soa_record.MName,
					rd->soa_record.RName,
					rd->soa_record.serial,
					rd->soa_record.refresh,
					rd->soa_record.retry,
					rd->soa_record.expire,
					rd->soa_record.minimum
				);
				break;
			case RR_PTR:
				printf("Pointer Resource Record { name '%s' }",
					rd->ptr_record.name
				);
				break;
			case RR_MX:
				printf("Mail Exchange Record { preference %u, exchange '%s' }",
					rd->mx_record.preference,
					rd->mx_record.exchange
				);
				break;
			case RR_TXT:
				printf("Text Resource Record { txt_data '%s' }",
					rd->txt_record.txt_data
				);
				break;
			case RR_AAAA:
				printf("AAAA Resource Record { address ");
			
				for(i = 0; i < 16; ++i)
					printf("%s%02x", (i ? ":" : ""), rd->aaaa_record.addr[i]);
			
				printf(" }");
				break;
			default:
				printf("Unknown Resource Record { ??? }");
		}
		printf("}\n");
		rr = rr->next;
	}
}

void print_query(struct Message* msg)
{
	printf("QUERY { ID: %02x", msg->id);
	printf(". FIELDS: [ QR: %u, OpCode: %u ]", msg->qr, msg->opcode);
	printf(", QDcount: %u", msg->qdCount);
	printf(", ANcount: %u", msg->anCount);
	printf(", NScount: %u", msg->nsCount);
	printf(", ARcount: %u,\n", msg->arCount);

	struct Question* q = msg->questions;
	while(q)
	{
		printf("  Question { qName '%s', qType %u, qClass %u }\n",
			q->qName,
			q->qType,
			q->qClass
		);
		q = q->next;
	}

	print_resource_record(msg->answers);
	print_resource_record(msg->authorities);
	print_resource_record(msg->additionals);

	printf("Clinet:%s", inet_ntoa(msg->cliaddr));

	printf("}\n");
}


/*
* Basic memory operations.
*/
uint8_t get8bits( const uint8_t** buffer ) {
	uint8_t value;

	memcpy( &value, *buffer, 1);
	*buffer += 1;

	return ntohs( value );
}

size_t get16bits( const uint8_t** buffer ) {
	uint16_t value;

	memcpy( &value, *buffer, 2 );
	*buffer += 2;

	return ntohs( value );
}

uint32_t get32bits( const uint8_t** buffer ) {
	uint32_t value;

	memcpy( &value, *buffer, 4 );
	*buffer += 4;

	return ntohl( value );
}

void put8bits( uint8_t** buffer, uint8_t value ) {
	memcpy( *buffer, &value, 1 );
	*buffer += 1;
}

void put16bits( uint8_t** buffer, uint16_t value ) {
	value = htons( value );
	memcpy( *buffer, &value, 2 );
	*buffer += 2;
}

void put32bits( uint8_t** buffer, uint32_t value ) {
	//value = htons( value );
	value = htonl( value );
	memcpy( *buffer, &value, 4 );
	*buffer += 4;
}

void putcname(uint8_t** buffer, const uint8_t* domain)
{
	uint8_t* buf = *buffer;
	const uint8_t* beg = domain;
	const uint8_t* pos;
	int len = 0;
	int i = 0;

	while(pos = strchr(beg, '.'))
	{
		len = pos - beg;
		buf[i] = len;
		i += 1;
		memcpy(buf+i, beg, len);
		i += len;

		beg = pos + 1;
	}
	len = strlen(domain) - (beg - domain);

	buf[i] = len;
	i += 1;
	memcpy(buf + i, beg, len);
	i += len;

	buf[i] = 0;
	i += 1;
	*buffer += i;
}



/*
* Deconding/Encoding functions.
*/

// 3foo3bar3com0 => foo.bar.com
char* decode_domain_name(const uint8_t** buffer)
{
	uint8_t name[DOMAINLEN];
	const uint8_t* buf = *buffer;
	int j = 0;
	int i = 0;
	while (buf[i] != 0)
	{
		//if(i >= buflen || i > sizeof(name))
		//	return NULL;
		
		if(i != 0)
		{
			name[j] = '.';
			j += 1;
		}

		int len = buf[i];
		i += 1;

		memcpy(name+j, buf + i, len);
		i += len;
		j += len;
	}

	name[j] = '\0';

	*buffer += i + 1; //also jump over the last 0

	return strdup(name);
}

// foo.bar.com => 3foo3bar3com0
void encode_domain_name(uint8_t** buffer, const uint8_t* domain)
{
	uint8_t* buf = *buffer;
	const uint8_t* beg = domain;
	const uint8_t* pos;
	int len = 0;
	int i = 0;

	while(pos = strchr(beg, '.'))
	{
		len = pos - beg;
		buf[i] = len;
		i += 1;
		memcpy(buf+i, beg, len);
		i += len;

		beg = pos + 1;
	}

	len = strlen(domain) - (beg - domain);

	buf[i] = len;
	i += 1;

	memcpy(buf + i, beg, len);
	i += len;

	buf[i] = 0;
	i += 1;

	*buffer += i;
}

void Message_decode_header(struct Message* msg, const uint8_t** buffer)
{
	msg->id = get16bits(buffer);

	uint32_t fields = get16bits(buffer);
	msg->qr = (fields & QR_MASK) >> 15;
	msg->opcode = (fields & OPCODE_MASK) >> 11;
	msg->aa = (fields & AA_MASK) >> 10;
	msg->tc = (fields & TC_MASK) >> 9;
	msg->rd = (fields & RD_MASK) >> 8;
	msg->ra = (fields & RA_MASK) >> 7;
	msg->rcode = (fields & RCODE_MASK) >> 0;

	msg->qdCount = get16bits(buffer);
	msg->anCount = get16bits(buffer);
	msg->nsCount = get16bits(buffer);
	msg->arCount = get16bits(buffer);
}

void Message_encode_header(struct Message* msg, uint8_t** buffer)
{
	put16bits(buffer, msg->id);

	int fields = 0;
	fields |= (msg->qr << 15) & QR_MASK;
	fields |= (msg->rcode << 0) & RCODE_MASK;
	// TODO: insert the rest of the fields
	put16bits(buffer, fields);

	put16bits(buffer, msg->qdCount);
	put16bits(buffer, msg->anCount);
	put16bits(buffer, msg->nsCount);
	put16bits(buffer, msg->arCount);
}

void Question_init(struct Question *que)
{
	memset(que, 0, sizeof(struct Question));
}

void Message_unpackage(struct Message *msg, const uint8_t *buffer, size_t *len)
//int Message_decode(struct Message* msg, const uint8_t* buffer, int size)
{
	char name[DOMAINLEN];
	int i;
	struct Question	*q = NULL;

	Message_decode_header(msg, &buffer);

	if((msg->anCount + msg->nsCount) != 0)
	{
		printf("Only questions expected!\n");
		*len = -1;
		return;
	}

	// parse questions
	uint32_t qcount = msg->qdCount;
	struct Question* qs = msg->questions;
	for (i = 0; i < qcount; ++i)
	{
		q = malloc(sizeof(struct Question));
		Question_init(q);

		q->qName = decode_domain_name(&buffer);
		q->qType = get16bits(&buffer);
		q->qClass = get16bits(&buffer);

		//prepend question to questions list
		q->next = qs; 
		msg->questions = q;
	}

	uint8_t  opt_owner;
	uint16_t opt_type;
	opt_owner = get8bits(&buffer);
	opt_type = get16bits(&buffer);
	if (opt_owner != 0 || opt_type != 41) /* Opt record */ 
	{
		/* Not EDNS.  */
		return;
	}
	uint16_t opt_class;
	uint8_t  opt_version;
	uint16_t opt_flags;
	uint16_t opt_rdlen;
	uint16_t opt_nsid;
	uint8_t  opt_rcode;

	uint16_t opt_code;
	uint16_t opt_len;
	uint16_t opt_FAMILY;
	uint8_t opt_src_mask;
	uint8_t opt_scope_mask;

	opt_class = get16bits(&buffer);
	opt_rcode = get8bits(&buffer);
	opt_version = get8bits(&buffer);
	opt_flags = get16bits(&buffer);
	opt_rdlen = get16bits(&buffer);

	if (opt_rdlen >= 12)
	{
		opt_code = get16bits(&buffer);
		opt_len = get16bits(&buffer);
		opt_FAMILY = get16bits(&buffer);          
		opt_src_mask = get8bits(&buffer);
		opt_scope_mask = get8bits(&buffer);
	}
	else
	{
		return;
	}

	printf("opt_rdlen:%d opt_code %d  opt_len %d\n", opt_rdlen, opt_code, opt_len);
	if (opt_len  >= 7 && opt_code == 8) /* Opt code edns subnet client */ 
	{                                      
		uint32_t addr = htonl(get32bits(&buffer));
		msg->cliaddr = *(struct in_addr *)&addr;
	//	printf( " >> %s\n", inet_ntoa(*(struct in_addr *)&addr));
	}

	// We do not expect any resource records to parse here.
	return;
}

struct ResourceRecord *RR_soa_create(const char *mname, const char *rname, uint32_t serial)
{
	struct ResourceRecord *tmp = NULL;
	tmp = malloc(sizeof(struct ResourceRecord));

	return tmp;
}

int Resolve_Record(const char *zone, const char *name, uint32_t *type, char *rdata, uint32_t *ttl)
{
	
	return 0;
}

int Resolve_SOA(const char *zone, const char *name, char *mname, char *rname, uint32_t *serial)
{
	sprintf(mname, "ns1.%s", zone);
	sprintf(rname, "root.%s", zone);
	*serial = 2017083016;
	return 0;
}

// For every question in the message add a appropiate resource record
// in either section 'answers', 'authorities' or 'additionals'.

int Message_resolve(struct Message *msg)
{
	struct ResourceRecord* beg;
	struct ResourceRecord* rr;
	struct Question* q;
	int rc;

	// leave most values intact for response
	msg->qr = 1; // this is a response
	msg->aa = 1; // this server is authoritative
	msg->ra = 0; // no recursion available
	msg->rcode = RT_NoError;

	//should already be 0
	msg->anCount = 0;
	msg->nsCount = 0;
	msg->arCount = 0;

	//for every question append resource records
	q = msg->questions;
	while (q)
	{
		rr = malloc(sizeof(struct ResourceRecord));

		rr->name = strdup(q->qName);
		rr->type = q->qType;
		rr->class = q->qClass;
		rr->ttl = (long)60 * 60; //in seconds; 0 means no caching
		
		printf("Query for '%s' type '%d'\n", q->qName,  q->qType);
		
		// We only can only answer two question types so far
		// and the answer (resource records) will be all put
		// into the answers list.
		// This behavior is probably non-standard!
		switch(q->qType)
		{
			case RR_A:
				rr->rd_length = 4;
				rc = get_A_Record(rr->rd_data.a_record.addr, q->qName);
				if(rc < 0)
					goto next;
				break;
			case RR_AAAA:
				rr->rd_length = 16;
				rc = get_AAAA_Record(rr->rd_data.aaaa_record.addr, q->qName);
				if(rc < 0)
					goto next;
				break;
			case RR_CNAME:
				rr->rd_length = strlen("aa.b.com") + 2;
				rr->rd_data.cname_record.name = strdup("aa.b.com");
				break;
			case RR_SOA:
				rr->rd_length = strlen(" ns1.b.com  root.b.com ") + 20;
				rr->rd_data.soa_record.MName = strdup("ns1.b.com");
				rr->rd_data.soa_record.RName = strdup("root.b.com");
				rr->rd_data.soa_record.serial = 2017182013;
				rr->rd_data.soa_record.refresh = DEFAULT_REFRESH;
				rr->rd_data.soa_record.retry = DEFAULT_RETRY;
				rr->rd_data.soa_record.expire = DEFAULT_EXPIRE;
				rr->rd_data.soa_record.minimum = DEFAULT_MINIMUM;
				break;
			/*
			case NS_RR:
			case CNAME_RR:
			case SOA_RR:
			case PTR_RR:
			case MX_RR:
			case TXT_RR:
			*/
			default:
				msg->rcode = RT_NotImp;
				printf("Cannot answer question of type %d.\n", q->qType);
				goto next;
		}

		msg->anCount++;
		// prepend resource record to answers list
		beg = msg->answers;
		msg->answers = rr;
		rr->next = beg;
		//jump here to omit question
next:
		// process next question
		q = q->next;
	}
}

int encode_resource_records(struct ResourceRecord* rr, uint8_t** buffer)
{
	int i;
	while(rr)
	{
		/* Answer questions by attaching resource sections. */
		putcname(buffer, rr->name);
		put16bits(buffer, rr->type);
		put16bits(buffer, rr->class);
		put32bits(buffer, rr->ttl);
		put16bits(buffer, rr->rd_length);
		
		switch(rr->type)
		{
			case RR_A:
				for(i = 0; i < 4; ++i)
					put8bits(buffer, rr->rd_data.a_record.addr[i]);
				break;
			case RR_AAAA:
				for(i = 0; i < 16; ++i)
					put8bits(buffer, rr->rd_data.aaaa_record.addr[i]);
				break;
			case RR_CNAME:
				putcname(buffer, rr->rd_data.cname_record.name);
				break;
			case RR_NS:
				putcname(buffer, rr->rd_data.ns_record.name);
				break;
			case RR_SOA:
				putcname(buffer, rr->rd_data.soa_record.MName);   /* Author Name Server */
				putcname(buffer, rr->rd_data.soa_record.RName);  /* mail of DNS */
				put32bits(buffer, rr->rd_data.soa_record.serial);   /* serial */
				put32bits(buffer, rr->rd_data.soa_record.refresh); /* refresh */
				put32bits(buffer, rr->rd_data.soa_record.retry); /* retry */
				put32bits(buffer, rr->rd_data.soa_record.expire); /* expire */
				put32bits(buffer, rr->rd_data.soa_record.minimum); /* minimum */
				break;
			default:
				fprintf(stderr, "Unknown type %u. => Ignore resource record.\n", rr->type);
			return 1;
		}
		
		rr = rr->next;
	}
	return 0;
}

int Message_encode(struct Message* msg, uint8_t** buffer)
{
	struct Question* q;
	int rc;

	Message_encode_header(msg, buffer);

	q = msg->questions;
	while(q)
	{
		encode_domain_name(buffer, q->qName);
		put16bits(buffer, q->qType);
		put16bits(buffer, q->qClass);

		q = q->next;
	}

	rc = 0;
	rc |= encode_resource_records(msg->answers, buffer);
	rc |= encode_resource_records(msg->authorities, buffer);
	rc |= encode_resource_records(msg->additionals, buffer);

	return rc;
}

void free_resource_records(struct ResourceRecord* rr)
{
	struct ResourceRecord* next;

	while(rr) {
		free(rr->name);
		next = rr->next;
		free(rr);
		rr = next;
	}
}

void free_questions(struct Question* qq)
{
	struct Question* next;

	while(qq) {
		free(qq->qName);
		next = qq->next;
		free(qq);
		qq = next;
	}
}

void Message_init(struct Message *msg)
{
	memset(msg, 0, sizeof(struct Message));
}

void Message_free(struct Message *msg)
{
	free_questions(msg->questions);
	free_resource_records(msg->answers);
	free_resource_records(msg->authorities);
	free_resource_records(msg->additionals);
}

#if 0
size_t Resolve_message(struct Message *msg, char *buffer, size_t len)
{
	size_t	size = 0;
	if (Message_decode(msg, buffer, len) != 0) 
	{
		return -1;
	}

	/* Print query */
	print_query(msg);
	resolver_process(msg);
	/* Print response */
	print_query(msg);

	uint8_t *p = buffer;
	if (Message_encode(msg, &p) != 0) {
		return -1;
	}

	size = ((char *)p - buffer);
	return size;
}
#endif

void Message_package(struct Message *msg, const uint8_t *data, size_t *len)
{
	uint8_t *p = (char *)data;
	if (Message_encode(msg, &p) != 0) 
	{
		*len = -1;
		return;
	}
	*len = ((char *)p - (char *)data);
}

int main(int argc, char *argv[])
{
	// buffer for input/output binary packet
	uint8_t buffer[BUF_SIZE];
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	int nbytes, rc, buflen;
	int sock;
	int port = 53;

	struct Message msg;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_DGRAM, 0);

	rc = bind(sock, (struct sockaddr*) &addr, addr_len);

	if(rc != 0)
	{
		printf("Could not bind: %s\n", strerror(errno));
		return 1;
	}

	printf("Listening on port %u.\n", port);

	while(1)
	{
		Message_init(&msg);
		nbytes = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *) &client_addr, &addr_len);
		msg.cliaddr = client_addr.sin_addr;

		Message_unpackage(&msg, buffer, (size_t *)&nbytes);
		Message_resolve(&msg);
		Message_package(&msg, buffer, (size_t *)&buflen);

#if 0
		buflen = Resolve_message(&msg, buffer, nbytes);
		if (Message_decode(&msg, buffer, nbytes) != 0) 
		{
			continue;
		}

		/* Print query */
		print_query(&msg);
		resolver_process(&msg);
		/* Print response */
		print_query(&msg);

		uint8_t *p = buffer;
		if (Message_encode(&msg, &p) != 0) {
			continue;
		}

		int buflen = p - buffer;
#endif
		sendto(sock, buffer, buflen, 0, (struct sockaddr*) &client_addr, addr_len);

		Message_free(&msg);
	}

	close(sock);

	return 0;
}
