/**
 * Implementation of message interface
 */
#include "mympidatatype.h"
#include "mymsg.h"
#include "debug.h"

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <errno.h>
#include <unistd.h>

#define MIN_MSG_LENGTH (sizeof(msg_t))

/**
 * For debugging purpose to print 
 * MPI_Datatype in string form.
 */
char *mympi_datatypes[] = {
    "MPI_CHAR",
    "MPI_INT",
    "MPI_DOUBLE"
};

char *mympi_types[] = {
    "MSG_INIT",
    "MSG_DATA"
};

/**
 * Utility functions for determining the endianess of the
 * system
 */
void __htonmsg(msg_t * msg);
void __ntohmsg(msg_t * msg);
uint32_t __getipaddress(char *hostname);

/**
 * This is used for purpose of data ordering to common format.
 * This function checks the endianess of the program.
 */
static inline int is_little_endian()
{
    union {
	uint16_t x;
	char c;
    } test;

    test.x = 1;
    return test.c;
}

/*
 * Free alloacted memory for message.
 */
static inline void __free_msg(msg_t * msg)
{
    if (msg != NULL) {
	free(msg);
    }
}

/**
 * This function allocates message object of size payload length
 */
static inline int allocate_msg(msg_t ** pMsg, int payload_length)
{
    //check arguments 
    if (pMsg == NULL) {
	return MSG_INVALID_ARG;
    }

    int total_msg_size = sizeof(msg_t) + payload_length;
    //allocate memory for msg
    *pMsg = (msg_t *) malloc(total_msg_size);

    //initialize memory
    memset(*pMsg, 0, total_msg_size);

    if (*pMsg == NULL) {
	return MSG_ERROR;
    }

    return MSG_SUCCESS;
}

/**
 * This function will take care of sending data of size n bytes
 * over fd.
 *
 * Lifter from stevens UNP_Vol_1
 */
int readn(int fd, void *vptr, unsigned int n)
{
    unsigned int nleft;
    unsigned int nread;
    char *ptr;

    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
	if ((nread = read(fd, ptr, nleft)) < 0) {
	    if (errno == EINTR) {
		nread = 0;
	    } else {
		return MSG_ERROR;
	    }
	} else if (nread == 0) {
	    break;
	}
	nleft -= nread;
	ptr += nread;
    }
    return (n - nleft);
}

/*
 * This function will take care of recieving data of size n bytes
 * over fd.
 *
 * Lifted from stevens UNP_Vol_1
 */
int writen(int fd, const void *vptr, unsigned int n)
{
    unsigned int nleft;
    unsigned int nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
	if ((nwritten = write(fd, ptr, nleft)) <= 0) {
	    if (nwritten < 0 && errno == EINTR)
		nwritten = 0;
	    else
		return MSG_ERROR;
	}

	nleft -= nwritten;
	ptr += nwritten;
    }

    return (n);
}

/**
 * This function reads message object from descriptor
 * and returns message object.
 *
 * Input parameters
 * 	fd   descriptor to read message from
 * Output parameters
 * 	pMsg message object
 * Return value
 * 	returns MSG_SUCCESS on succssesful receive
 * 	or MSG_ERROR in case of failure.
 */
int read_msg(int fd, msg_t ** pMsg)
{
    if (!fd) {
	return MSG_ERROR;
    }
    //message payload length
    int msg_length;

    //read length of the message
    if (read(fd, &msg_length, LENGTH_SIZE) != LENGTH_SIZE) {
	dprintf("Failed to read length \n");
	return MSG_ERROR;
    }
    //allocate memory for local buffer of message length size
    char *msg_buffer = (char *) malloc(MSG_SIZE(msg_length));
    if (!msg_buffer) {
	dprintf
	    ("Failed to allocate memory for local buffer of size %lu \n",
	     MSG_SIZE(msg_length));
	return MSG_ERROR;
    }
    //copy length to msg_buffer
    memcpy(msg_buffer, &msg_length, LENGTH_SIZE);

    //read entire message from the buffer
    if (readn
	(fd, msg_buffer + LENGTH_SIZE,
	 MSG_SIZE(msg_length - LENGTH_SIZE)) !=
	(MSG_SIZE(msg_length - LENGTH_SIZE))) {
	dprintf("failed to read %d bytes\n", msg_length);
	return MSG_ERROR;
    }
    //parse the buffer into message
    if (parse_msg(msg_buffer, msg_length, pMsg) != MSG_SUCCESS) {
	dprintf("failed to parse message\n");
	return MSG_ERROR;
    }
    //clean up memory
    free(msg_buffer);

    return MSG_SUCCESS;
}


/**
 * This function sends message object over descriptor.
 *
 * Input parameters
 * 	fd   descriptor to write message to
 *      pMsg message object to be sent
 * Return value
 * 	returns MSG_SUCCESS of successful receive 
 * 	or MSG_ERROR in case of failure.
 */
int send_msg(int fd, msg_t * pMsg)
{
    if (!fd) {
	return MSG_ERROR;
    }
    //write the message over descriptor
    if (writen(fd, pMsg, MSG_SIZE(pMsg->length)) != MSG_SIZE(pMsg->length)) {
	dprintf("Failed to write data of size:%lu \n",
		MSG_SIZE(pMsg->length));
	return MSG_ERROR;
    }

    return MSG_SUCCESS;
}

/**
 * Fill in msg structure by parsing buffer upto size length.
 */
int parse_msg(void *buffer, unsigned int length, msg_t ** pMsg)
{
    int status = allocate_msg(pMsg, length);
    if (status != MSG_SUCCESS) {
	return status;
    }

    msg_t *msg = *pMsg;

    //size of msg_t.length is uint32_t
    memcpy(&msg->length, buffer + LENGTH_OFFSET, LENGTH_SIZE);
    memcpy(&msg->type, buffer + TYPE_OFFSET, TYPE_SIZE);

    //init message
    if (msg->type & MSG_INIT) {
	//check payload message length
	if (msg->length != 0) {
	    return MSG_INVALID_INIT_MSG;
	}
	//copy init hdr fields
	memcpy(&(msg->init.port), buffer + INIT_HDR_PORT_OFFSET,
	       PORT_SIZE);
	memcpy(&(msg->init.rank), buffer + INIT_HDR_RANK_OFFSET,
	       RANK_SIZE);
	memcpy(&(msg->init.address), buffer + INIT_HDR_ADDRESS_OFFSET,
	       ADDRESS_SIZE);
    } else if (msg->type & MSG_DATA) {
	//check payload length
	if (msg->length != length) {
	    return MSG_INVALID_DATA_MSG;
	}
	//copy data hdr fields
	memcpy(&(msg->data.datatype), buffer + DATA_HDR_DATATYPE_OFFSET,
	       DATATYPE_SIZE);
	memcpy(&(msg->data.tag), buffer + DATA_HDR_TAG_OFFSET, TAG_SIZE);
	memcpy(&(msg->payload), buffer + DATA_PAYLOAD_OFFSET, length);
    } else {
	return MSG_INVALID_MSG;
    }

    //__htonmsg(msg);
    return MSG_SUCCESS;
}


/*
 * This function populates msg structure and converts each of the
 * data type to network byte order.
 */
int create_init_msg(int rank, int port, msg_t ** pMsg)
{
    int status = allocate_msg(pMsg, 0);
    if (status != MSG_SUCCESS) {
	return status;
    }

    msg_t *msg = *pMsg;

    msg->length = 0;
    msg->type = MSG_INIT;
    msg->init.port = port;
    msg->init.rank = rank;
    msg->init.address = __getipaddress("127.0.0.1");

    //convert to network byte order  
    //__htonmsg(msg);
    return 0;
}

/*
 * This function populates msg structure and converts each of the
 * data type to network byte order except for payload.
 */
int create_data_msg(MPI_Datatype datatype, unsigned int tag, void *buffer,
		    int length, msg_t ** pMsg)
{
    int status = allocate_msg(pMsg, length);
    if (status != MSG_SUCCESS) {
	return status;
    }
    msg_t *msg = *pMsg;

    msg->length = length;
    msg->type = MSG_DATA;
    msg->data.datatype = datatype;
    msg->data.tag = tag;
    memcpy(&(msg->payload), buffer, length);

    //convert to host byte order
    //__ntohmsg(msg);
    return 0;
}

/*
 * This function converts message in host byte order to network byte order
 */
void __htonmsg(msg_t * msg)
{
    if (!is_little_endian()) {
	return;
    }

    msg->length = htonl(msg->length);
    msg->type = htons(msg->type);

    if (msg->type | MSG_INIT) {
	msg->init.port = htons(msg->init.port);
	msg->init.rank = htonl(msg->init.rank);
	msg->init.address = htonl(msg->init.address);
    } else if (msg->type | MSG_DATA) {
	msg->data.datatype = htons(msg->data.datatype);
	msg->data.tag = htonl(msg->data.tag);
    } else {
	dprintf("Invalid message type: msg:%p\n", msg);
    }

}

/*
 * This function converts message in network byte order to host byte order
 */
void __ntohmsg(msg_t * msg)
{
    if (!is_little_endian()) {
	return;
    }

    msg->length = ntohl(msg->length);
    msg->type = ntohs(msg->type);

    if (msg->type | MSG_INIT) {
	msg->init.port = ntohs(msg->init.port);
	msg->init.rank = ntohl(msg->init.rank);
	msg->init.address = ntohl(msg->init.address);
    } else if (msg->type | MSG_DATA) {
	msg->data.datatype = ntohs(msg->data.datatype);
	msg->data.tag = ntohl(msg->data.tag);
    } else {
	dprintf("Invalid message type msg:%p\n", msg);
    }
}

/*This method converts host name to ip address
 *
 * REFERENCE: http://www.binarytides.com/blog/get-ip-address-from-hostname-in-c-using-linux-sockets
 */
uint32_t __getipaddress(char *hostname)
{
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in *h;
    uint32_t ip;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;	// use AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, "http", &hints, &servinfo)) != 0) {
	dprintf("getaddrinfo: %s\n", gai_strerror(rv));
	return 0;
    }
    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
	h = (struct sockaddr_in *) p->ai_addr;
	ip = h->sin_addr.s_addr;
    }

    freeaddrinfo(servinfo);	// all done with this structure
    return ip;
}

/**
 * Print message headers
 */
void print_msg_hdr(msg_t * msg)
{
    if (!msg) {
	return;
    }
    dprintf("*******message header contents*********\n");
    dprintf("length:%u\n", msg->length);

    if (msg->type & MSG_INIT) {
	dprintf("type:%s\n", mympi_types[0]);
	dprintf("rank:%u\n", msg->init.rank);
	dprintf("address:%u\n", msg->init.address);
	dprintf("port:%u\n", msg->init.port);
    } else if (msg->type & MSG_DATA) {
	dprintf("type:%s\n", mympi_types[1]);
	dprintf("tag:%u\n", msg->data.tag);
	dprintf("datatype:%s\n", mympi_datatypes[msg->data.datatype]);
    } else {
	dprintf("Invalid message:%p\n", msg);
    }

    dprintf("*******message header ends************\n");
}

void free_init_msg(msg_t * msg)
{
    __free_msg(msg);
}


void free_data_msg(msg_t * msg)
{
    __free_msg(msg);
}
