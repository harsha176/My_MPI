/**
 * This header defines the messages format used in communication between nodes.
 */
#ifndef __MSG_H_
#define __MSG_H_

#include "mympidatatype.h"
#include <stdint.h>

/*Message error codes*/
#define MSG_SUCCESS           0
#define MSG_ERROR            -1
#define MSG_INVALID_ARG      -2
#define MSG_PARSE_ERROR      -3
#define MSG_INVALID_INIT_MSG -4
#define MSG_INVALID_DATA_MSG -5
#define MSG_INVALID_MSG      -6

/*Message types*/
#define MSG_INIT    1 //Initialization message
#define MSG_DATA    2 //Data message

extern char* mympi_types[];

/**
 * For each message type update the union in msg_t and define offsets and sizes
 * for these fields. Finally, write create_xxx_msg, free_xxx_msg and parse_msg functions.
 */
/*Initialization message header*/
struct init_hdr {
   uint32_t rank;       /*rank of the processor*/
   uint32_t address;    /*Internet address*/
   uint16_t port;       /*port number of listening server of the processor*/
   uint16_t padding;    /*padding*/
};

/*Data message header*/
struct data_hdr {
   uint32_t tag;        /*tag*/
   uint32_t padding;    /*padding*/
   uint32_t datatype;   /*Data type of the message*/
};


/*Message format*/
struct __msg_t {
   uint32_t length;    /*payload length of the message*/
   uint32_t type;      /*type of the message to be exchanged*/
   /*
    * specific headers for each message type
    * each header is of constant size 12 bytes
    */  
   union {
      struct init_hdr init;  /*initialization message header*/
      struct data_hdr data;  /*data message header*/
   };
   char payload[0];   
};

typedef struct __msg_t msg_t;

#define MIN_MSG_LENGTH (sizeof(msg_t))

/*msg field size*/
#define LENGTH_SIZE       sizeof(((msg_t*)0)->length)
#define TYPE_SIZE 	  sizeof(((msg_t*)0)->type)

/*Init header field sizes*/
#define PORT_SIZE 	  sizeof(((msg_t*)0)->init.port)
#define RANK_SIZE         sizeof(((msg_t*)0)->init.rank)
#define ADDRESS_SIZE      sizeof(((msg_t*)0)->init.address)

/*Data message field sizes*/
#define DATATYPE_SIZE     sizeof(((msg_t*)0)->data.datatype)
#define TAG_SIZE          sizeof(((msg_t*)0)->data.tag)
#define PADDING_SIZE      sizeof(((msg_t*)0)->data.padding)

/*Offset of a member in a structure*/
#define OFFSETOF(type, field)    ((unsigned long) &(((type *) 0)->field))

/*Offset calculation*/
#define LENGTH_OFFSET             OFFSETOF(msg_t, length)
#define TYPE_OFFSET               OFFSETOF(msg_t, type)

/*Init header offsets*/
#define INIT_HDR_RANK_OFFSET      OFFSETOF(msg_t, init.rank)
#define INIT_HDR_ADDRESS_OFFSET   OFFSETOF(msg_t, init.address)
#define INIT_HDR_PORT_OFFSET      OFFSETOF(msg_t, init.port)

/*Data header offsets*/
#define DATA_HDR_TAG_OFFSET       OFFSETOF(msg_t, data.tag)
#define DATA_HDR_DATATYPE_OFFSET  OFFSETOF(msg_t, data.datatype) 
#define DATA_PAYLOAD_OFFSET       OFFSETOF(msg_t, payload)

/*Calculate message size from length of payload*/
#define MSG_SIZE(x) ((MIN_MSG_LENGTH) + (x))

/*
 * This function creates initialization message.
 * Input parameters
 *    rank    processor rank
 *    port    server port of processor
 * Output parameters
 *    msg     message
 * Return value
 *   MSG_SUCCESS on successful creation of message
 *   MSG_ERROR   on error 
 */
int create_init_msg(int /*rank*/, int /*port*/, msg_t** /*msg*/);

/*
 * This function creates data message.
 * Input parameres
 * 	MPI_    Datatype datatype of message
 *      tag     message tag 
 *      buffer  payload data
 *      length  payload length
 * Output parameters
 * 	msg     message structure
 * Return value
 *     MSG_SUCCESS on successful creation of message 
 *     MSG_ERROR   on error message
 */
int create_data_msg(MPI_Datatype /*datatype*/, unsigned int /*tag*/, 
                    void* /*buffer*/, int /*length*/, msg_t** /*msg*/);

/*
 * This function parses message. 
 * Input parametes
 * 	buffer   data buffer to be parsed
 *      length   length of the data buffer to be parsed
 * Output parameters
 *      msg      message structure
 * Return value
 * 	MSG_SUCCESS on successful parsing of the buffer data
 * 	MSG_PARSE_ERROR on failure
 */
int parse_msg(void* /*buffer*/, unsigned int /*length*/, msg_t** /*msg*/);

/**
 * This is an utility function to prints message header.
 */
void print_msg_hdr(msg_t* msg);

/*
 * This function free's data for init message.
 */
void free_init_msg(msg_t* /*msg*/);

/*
 * This function free's data for data message.
 */
void free_data_msg(msg_t* /*msg*/);

#endif
