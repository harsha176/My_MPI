#include "mympidatatype.h"
#include "mymsg.h"
#define DEBUG
#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{

    //msg_t msg;
    char buffer[100];
    uint32_t length;
    FILE *fp = fopen("msg.txt", "w");
    char *text = "HappyReturnsinTelugu";
    msg_t *pmsg;
    if (create_data_msg
	(MPI_INT, 12, text, sizeof(char) * (strlen(text) + 1),
	 &pmsg) != MSG_SUCCESS) {
	fprintf(stderr, "failed to create message\n");
    }

    /*if(create_init_msg(3, 9000, &pmsg) != MSG_SUCCESS) {
       fprintf(stderr, "failed to create message\n");
       } */

    if (fwrite(pmsg, MSG_SIZE(pmsg->length), 1, fp) != 1) {
	fprintf(stderr, "Failed to write data\n");
    }
    free_init_msg(pmsg);

    fclose(fp);
    fp = fopen("msg.txt", "r");

    int fd = fileno(fp);
    if (read(fd, &buffer, LENGTH_SIZE) != LENGTH_SIZE) {
	fprintf(stderr, "Failed to read length\n");
    }

    memcpy(&length, buffer, LENGTH_SIZE);
    //get back
    if (lseek(fd, -4, SEEK_CUR) != 0) {
	perror("Failed to reset cursor");
	return -1;
    }

    if (read(fd, buffer, MSG_SIZE(length)) != MSG_SIZE(length)) {
	fprintf(stderr, "failed to read %d bytes\n", length);
    }
    msg_t *rmsg;
    if (parse_msg(buffer, length, &rmsg) != MSG_SUCCESS) {
	fprintf(stderr, "failed to parse message\n");
    }

    print_msg_hdr(rmsg);
    free_data_msg(rmsg);
    fclose(fp);
    return 0;
}
