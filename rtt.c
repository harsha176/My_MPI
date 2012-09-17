/**
 * This program benchmarks latency in communication component of MPI for various message sizes
 * and node topology.
 */
#include "mympi.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define NR_RTT_ITR 8
#define MSG_START_EXP  3
#define MSG_END_EXP 22
#define DEBUG 0

int main(int argc, char *argv[])
{

    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int processor_name_len;
    int nr_nodes, rank;

    MPI_Status status;

    //possible message sizes
    int msg_init_size = 1 << MSG_START_EXP;	//message intial size 8 bytes
    //int msg_step_size = 1 << 2;	//message step size 8 bytes
    int msg_last_size = 1 << MSG_END_EXP;	//message final size
    int nr_msgs = MSG_END_EXP - MSG_START_EXP + 1;	//number of different message sizes

    //Initialize
    MPI_Init(&argc, &argv);

    //retrieve nr_process and current process rank
    MPI_Comm_size(MPI_COMM_WORLD, &nr_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    MPI_Get_processor_name(processor_name, &processor_name_len);
    if (DEBUG) {
	fprintf(stderr, "Processor name:%s and its rank is:%d\n",
		processor_name, rank);
    }

    if (rank == 0) {
	/*
	 * Calculate rtt for each message size to each node.
	 *
	 * Send a message of size curr_msg_size to each node and wait till
	 * the other process echoes same message
	 */
	int curr_msg_size;
	char *buffer;

	//stats(min, avg, max) array for each message for all nodes
	double *stats = (double *) malloc(sizeof(double) * nr_nodes * 3);
	if (!stats) {
	    fprintf(stderr, "Failed to calculate statistics");
	    goto fail;
	}

	int i = 0;
	curr_msg_size = msg_init_size;
	for (; i < nr_msgs; i++) {
	    //allocate buffer memory for curr_msg_size
	    buffer = (char *) malloc(sizeof(char) * curr_msg_size);
	    if (!buffer) {
		perror("Failed to send data to other nodes");
		goto fail;
	    }

	    /*Send data to all process */
	    int curr_node = 1;	//skip first node
	    for (; curr_node < nr_nodes; curr_node++) {
		/*Calculate rtt for all iterations */
		int i;
		double min_rtt, cum_rtt, max_rtt;
		min_rtt = 99999;
		cum_rtt = 0;
		max_rtt = 0;
		//intialize with invalid values
		double start_time, end_time, rtt_time;
		for (i = 0; i < NR_RTT_ITR; i++) {
		    if (DEBUG) {
			fprintf(stderr,
				"Sending message of size %d to %d node %dth time",
				curr_msg_size, curr_node, i);
		    }
		    //start timer for curr_node
		    start_time = MPI_Wtime();

		    if (MPI_Send
			((void *) buffer, curr_msg_size, MPI_CHAR,
			 curr_node, 0, MPI_COMM_WORLD) != MPI_SUCCESS) {
			fprintf(stderr,
				"Failed to send message of size %d to %d node\n",
				curr_msg_size, curr_node);
			goto fail;
		    }

		    if (MPI_Recv
			((void *) buffer, curr_msg_size, MPI_CHAR,
			 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
			 &status) != MPI_SUCCESS) {
			fprintf(stderr,
				"Failed to receive message of size %d to %d node\n",
				curr_msg_size, curr_node);
			goto fail;
		    }
		    //validate received message
		    assert(curr_node == status.MPI_SOURCE);
		    int actual_rcvd_size;
		    MPI_Get_count(&status, MPI_CHAR, &actual_rcvd_size);
		    assert(curr_msg_size == actual_rcvd_size);

		    if (DEBUG) {
			fprintf(stderr,
				"Recieved message of size %d from %d node\n",
				actual_rcvd_size, status.MPI_SOURCE);
		    }
		    //end timer for curr_node
		    end_time = MPI_Wtime();
		    //calculate rtt
		    rtt_time = end_time - start_time;

		    if (DEBUG) {
			fprintf(stderr, " rtt_time:%e\n", rtt_time);
		    }
		    //update statistics
		    if (i != 0) {
			if (min_rtt > rtt_time) {
			    min_rtt = rtt_time;
			}
			if (max_rtt < rtt_time) {
			    max_rtt = rtt_time;
			}
			cum_rtt = cum_rtt + rtt_time;
		    }
		}		//end of sending message of curr_size to curr_node NR_RTT_ITR times

		//save statistics for curr_node
		stats[curr_node * 3] = min_rtt;
		stats[curr_node * 3 + 1] = cum_rtt / (NR_RTT_ITR - 1);
		stats[curr_node * 3 + 2] = max_rtt;
	    }			// end of sending message of curr_size to all nodes


	    //print statistics for particular message size
	    fprintf(stderr, "%-7d ", curr_msg_size);
	    int i = 1;
	    for (; i < nr_nodes; i++) {
		fprintf(stderr, "%e %e %e ", stats[i * 3 + 0],
			stats[i * 3 + 1], stats[i * 3 + 2]);
	    }
	    fprintf(stderr, "\n");

	    curr_msg_size *= 2;

	    //free memory for stats
	    //free(stats);

	    //free buffer memory
	    //free(buffer);
	}			// end of sending messages to all nodes
    } else {
	/*Non root process */
	//Receive and send message
	char *buffer = (char *) malloc(sizeof(char) * msg_last_size);
	if (!buffer) {
	    perror("Failed to allocate receive buffer");
	    goto fail;
	}
	int rcvd_msg_size;

	int i = 0;

	for (; i < NR_RTT_ITR * nr_msgs; i++) {
	    if (MPI_Recv
		((void *) buffer, msg_last_size, MPI_CHAR, 0, MPI_ANY_TAG,
		 MPI_COMM_WORLD, &status) != MPI_SUCCESS) {
		fprintf(stderr,
			"Failed to receive message of size node\n");
		goto fail;
	    }
	    if (MPI_Get_count(&status, MPI_CHAR, &rcvd_msg_size) !=
		MPI_SUCCESS) {
		fprintf(stderr,
			"Failed to retrieve data from root node\n");
	    }

	    if (MPI_Send
		((void *) buffer, rcvd_msg_size, MPI_CHAR, 0, 0,
		 MPI_COMM_WORLD) != MPI_SUCCESS) {
		fprintf(stderr,
			"Failed to send message of size %d to root node\n",
			rcvd_msg_size);
		goto fail;
	    }
	}

	free(buffer);
    }


    MPI_Finalize();
    return 0;

  fail:
    MPI_Finalize();
    return -1;
}
