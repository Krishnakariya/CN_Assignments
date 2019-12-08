#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h> //* to store address info.
#include <arpa/inet.h> //* to convert nw addresse to correct format.
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <string.h> 
#include <sys/ioctl.h>

#define FRAME_SIZE sizeof(frame)
#define PORT1 4000
#define PORT2 5000
#define PROCESSING_DELAY 2
#define PRPOGATION_DELAY 2.2
#define PACKET_DROP_PROBABILITY 30 //In terms of 1000
#define NO_OF_FRAMES 10
//* doubt think about which files to not include.
struct timer
{
	struct timeval start; 
	int stopped;
};

#define MAX_SEQ 7
typedef enum {frame_arrival, cksum_err, timeout, network_layer_ready, no_event} event_type; 
#include "protocol.h"

int is_frame_available_in_socket();
int is_timeout();

int frames_sent = 0;
int frames_received = 0;

struct simulation_packet
{
	double first_time_sent;
	int no_of_attempts;
	// char message[1024];
};

struct simulation_received_packet
{
	double first_time_received;
	// char message[1024];
};

struct timeval starting_time;
struct simulation_packet* packetarr;
struct simulation_received_packet* received_packet;
//* Return true if a <= b < c circularly; false otherwise. */
static boolean between(seq_nr a, seq_nr b, seq_nr c)
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a))) 
		return(true);
	else 
		return(false);
}

static void send_data(seq_nr frame_nr, seq_nr frame_expected, packet buffer[]) // doubt
{
	/* Construct and send a data frame. */ 
	frame s; /* scratch variable */
	s.info = buffer[frame_nr]; /* insert packet into frame */
	s.seq = frame_nr;  /* insert sequence number into frame */
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1); /* piggyback ack */ // doubt
    to_physical_layer(&s); /* transmit the frame */
    start_timer(frame_nr); /* start the timer running */
}


void protocol5(void)
{
	seq_nr next_frame_to_send; /* MAX SEQ > 1; used for outbound stream */
	seq_nr ack_expected; /* oldest frame as yet unacknowledged */
	seq_nr frame_expected; /* next frame expected on inbound stream */
	frame r; /* scratch variable */
	packet buffer[MAX_SEQ + 1]; /* buffers for the outbound stream */
	seq_nr nbuffered; /* number of output buffers currently in use */
	seq_nr i; /* used to index into the buffer array */
	event_type event; 

	enable_network_layer(); /* allow network layer ready events */
	ack_expected = 0; /* next ack expected inbound */
	next_frame_to_send = 0; /* next frame going out */ 
	frame_expected = 0; /* number of frame expected inbound */
	nbuffered = 0; /* initially no packets are buffered */
	
	while (true)
	{
		wait_for_event(&event); /* four possibilities: see event type above */
		switch(event)
		{
			case network_layer_ready: /* the network layer has a packet to send */
				/* Accept, save, and transmit a new frame. */
				printf("Sending data\n");
				from_network_layer(&buffer[next_frame_to_send]); /* fetch new packet */
				nbuffered = nbuffered + 1; /* expand the sender’s window */
				// printf("%d\n", nbuffered);
				send_data(next_frame_to_send, frame_expected, buffer);/* transmit the frame */
				inc(next_frame_to_send); /* advance sender’s upper window edge */
				break;

			case frame_arrival:	/* a data or control frame has arrived */
				printf("Frame Arrival\n");
				from_physical_layer(&r); /* get incoming frame from physical layer */
				if (r.seq == frame_expected) 
				{
					/* Frames are accepted only in order. */
					to_network_layer(&r.info); /* pass packet to network layer */
					inc(frame_expected); /* advance lower edge of receiver’s window */
				}
				/*Ack n implies n − 1, n − 2, etc. Check for this. */
				while (between(ack_expected, r.ack, next_frame_to_send))
				{
					/* Handle piggybacked ack. */
					nbuffered = nbuffered - 1; /* one frame fewer buffered */
					stop_timer(ack_expected); /* frame arrived intact; stop timer */
					inc(ack_expected);
				}
				break;

			case cksum_err: break; /* just ignore bad frames */

			case no_event: 
				// printf("No Event\n");
				break;

			case timeout: /* trouble; retransmit all outstanding frames */
				printf("timeout\n");
				next_frame_to_send = ack_expected; /* start retransmitting here */
				for (i = 1; i <= nbuffered; i++) 
				{
					send_data(next_frame_to_send, frame_expected, buffer);/* resend frame */
					inc(next_frame_to_send);/* prepare to send the next one */
				}
		}

		if (nbuffered < MAX_SEQ) 
		{
			enable_network_layer();
		}
		else
		{
			disable_network_layer();
		}

		if(frames_received >= NO_OF_FRAMES && frames_sent >= NO_OF_FRAMES)
		{
			printf("Analysis of Frames sent me :\n");
			int average_attempts = 0;
			for (int i = 0; i < NO_OF_FRAMES; ++i)
			{
				printf("Frame no: %d, First Time Sent : %lf, No of attempts: %d\n", i, packetarr[i].first_time_sent, packetarr[i].no_of_attempts);
				average_attempts += packetarr[i].no_of_attempts;
			}
			printf("Average no of attempts to send packets : %d\n",average_attempts );
			printf("Analysis of Frames Received by me :\n");
			for (int i = 0; i < NO_OF_FRAMES; ++i)
			{
				printf("Frame no: %d, First Time Received : %lf \n", i, received_packet[i].first_time_received);
			}
			// break;
		}
	}
 }


boolean network_layer_is_enabled = true;
// double propagation_delay = 2; //* round trip delay.

struct timer timer_array[MAX_SEQ];
int sender_fd, receiver_fd, server_socket;
frame received_frame;
char* data_to_send;
struct timeval end;
/* Wait for an event to happen; return its type in event. */
void wait_for_event(event_type *event)
{
	int num = rand()%3;
	switch(num)
	{
		case 0:
			if (is_frame_available_in_socket())
			{
				*event = frame_arrival;
			}
			else
			{
				*event = no_event;
			}
			break;
		case 1:
			if(is_timeout())
			{
				*event = timeout;
			}
			else
			{
				*event = no_event;
			}
			break;
		case 2:
			if(network_layer_is_enabled == true)
			{
				*event = network_layer_ready;			
			}	
			else
			{
				*event = no_event;
			}
			break;
	}
}

/* Fetch a packet from the network layer for transmission on the channel. */
void from_network_layer(packet *p)
{
	static int count=1;
	strcpy((char*)(p->data), data_to_send);
	snprintf((char*)(p->data), 13,"message: %d",count); // doubt
	count+=1;
	struct timeval start;
	gettimeofday(&(start), NULL);
	p->packet_no = frames_sent;
	if(p->packet_no < NO_OF_FRAMES)
	{
		double time_taken = (start.tv_sec - starting_time.tv_sec) + (start.tv_usec - starting_time.tv_usec)*1e-6;
		packetarr[p->packet_no].first_time_sent = time_taken;	
	}
	frames_sent+=1;
	// 1024 thing is sending!
}

/* Deliver information from an inbound frame to the network layer. */ 
void to_network_layer(packet *p)
{
	struct timeval received;
	gettimeofday(&(received), NULL);
	if(p->packet_no < NO_OF_FRAMES)
	{
		double time_taken = (received.tv_sec - starting_time.tv_sec) + (received.tv_usec - starting_time.tv_usec)*1e-6;
		received_packet[p->packet_no].first_time_received = time_taken;
	}
	printf("data received: %s\n", p->data);
}

/* Go get an inbound frame from the physical layer and copy it to r. */ 
void from_physical_layer(frame *r)
{
	frames_received+=1;
	r->info.packet_no = received_frame.info.packet_no;
	r->kind = received_frame.kind;
	r->seq = received_frame.seq;
	r->ack = received_frame.ack;
	strcpy((char*)r->info.data, (char*)received_frame.info.data);
	sleep(PROCESSING_DELAY);
}

int is_frame_available_in_socket()
{
	int count;
	ioctl(receiver_fd, FIONREAD, &count);
	if (count>=FRAME_SIZE)
	{
		int ret;
		int size = sizeof(received_frame);
		ret = recv(receiver_fd, &received_frame, size, 0);
			if (ret<0) { printf("E9: Error in receiving "); exit(0); } 
			// printf("Frame Received: %s\n",received_frame.info.data );
		int num = rand()%1000;
		if(num < PACKET_DROP_PROBABILITY)
		{
			return 0;
		}
		return 1;
	}
	return 0;
		
}

/* Pass the frame to the physical layer for transmission. */ 
void to_physical_layer(frame *s)
{
	if(s->info.packet_no < NO_OF_FRAMES)
	{
		packetarr[s->info.packet_no].no_of_attempts+=1;		
	}
	int ret;
	int size = sizeof(*s);
	ret = send(sender_fd, s, size, 0);
		if (ret<0) { printf("E5: Error in sending "); exit(0); } 
		printf("Frame Sent: %s\n",s->info.data );
}

/* Start the clock running and enable the timeout event. */ 
void start_timer(seq_nr k)
{
	gettimeofday(&(timer_array[k].start), NULL);
	timer_array[k].stopped = 0;
}

/* Stop the clock and disable the timeout event. */ 
void stop_timer(seq_nr k)
{
	timer_array[k].stopped = 1;
}

int is_timeout()
{
	gettimeofday(&(end), NULL);
	for (int k = 0; k < MAX_SEQ; ++k)
	{
		if(timer_array[k].stopped == 0)
		{
			double time_taken = (end.tv_sec - timer_array[k].start.tv_sec) + (end.tv_usec - timer_array[k].start.tv_usec)*1e-6;
			// printf("%lf\n", time_taken);
			if(time_taken >= PRPOGATION_DELAY)
			{
				printf("%lf\n", time_taken);
				return 1;
			}
		}
	}
	return 0;
}

/* Allow the network layer to cause a network layer ready event. */ 
void enable_network_layer(void)
{
	if(frames_sent == NO_OF_FRAMES)
	{
		return;
	}
	network_layer_is_enabled = true;
}

/* Forbid the network layer from causing a network layer ready event. */ 
void disable_network_layer(void)
{
	network_layer_is_enabled = false;
}

// /* Start an auxiliary timer and enable the ack timeout event. */ 
// void start_ack_timer(void)
// {

// }

// /* Stop the auxiliary timer and disable the ack timeout event. */ 
// void stop_ack_timer(void)
// {

// }

//* SIGINT signal handler to close client file descriptor upon receiving a SIGINT.
void handler(int sig)
{
	printf("Analysis of Frames sent me :\n");
	int average_attempts = 0;
	for (int i = 0; i < NO_OF_FRAMES; ++i)
	{
		printf("Frame no: %d, First Time Sent : %lf, No of attempts: %d \n", i, packetarr[i].first_time_sent, packetarr[i].no_of_attempts);
		average_attempts += packetarr[i].no_of_attempts;
	}
	printf("Average no of attempts to send packets : %d\n",average_attempts );
	printf("Analysis of Frames Received by me :\n");
	for (int i = 0; i < NO_OF_FRAMES; ++i)
	{
		printf("Frame no: %d, First Time Received : %lf \n", i, received_packet[i].first_time_received);
	}

	close(receiver_fd);

	sleep(2);

	close(sender_fd);
	close(server_socket);
	exit(0);
}

int main(int argc, char const *argv[])
{
	packetarr = (struct simulation_packet*)malloc(NO_OF_FRAMES*sizeof(struct simulation_packet));
	received_packet = (struct simulation_received_packet *)malloc(NO_OF_FRAMES*sizeof(struct simulation_received_packet));
	for (int i=0; i < NO_OF_FRAMES; i++)
	{
		packetarr[i].first_time_sent = 0;
		packetarr[i].no_of_attempts = 0;
		received_packet[i].first_time_received = 0;
	}


	for (int i = 0; i < MAX_SEQ; ++i)
	{
		gettimeofday(&(timer_array[i].start), NULL);
		timer_array[i].stopped = 1;
	}

	data_to_send = (char*)malloc(1024);
	for (int i=0; i<1024; i++)
	{
		data_to_send[i] = 'b';
	}
	data_to_send[1023] = '\0';

	//* variable to check correct return values of library and system calls.
	int ret;



	//* installing the SIGINT signal handler.
	signal(SIGINT, handler);



	//* create the server socket.
	server_socket = socket(AF_INET, SOCK_STREAM, 0); // domain: ipv4 internet protocols, type:SOCK_STREAM , flag:0. 



	//* define server's socket address.
	struct sockaddr_in server_address2;
	server_address2.sin_family = AF_INET;
	server_address2.sin_port = htons(PORT1);
	// inet_aton("192.168.43.66", &server_address.sin_addr);
	server_address2.sin_addr.s_addr = INADDR_ANY;
	int addrlen = sizeof(server_address2); 



	//* bind to our specified IP adress and port.
	bind(server_socket, (struct sockaddr*)&server_address2, sizeof(server_address2));
	

	
	//* listen for connections.
	listen(server_socket, 100);



	//* trying to accept an incoming connection from a client.
	printf("waiting for a client to connect. \n");
	sender_fd = accept(server_socket, (struct sockaddr*)(&server_address2), (socklen_t *)(&addrlen));
		printf("connected to client in file descriptor: %d\n", sender_fd);
			



	sleep(2);
	//* creating the client socket.
	receiver_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (receiver_fd<0) { printf("E0: 1"); exit(0); }



	//* specify IP address and port no. for the socket.
	struct sockaddr_in server_address1;
	server_address1.sin_family = AF_INET;
	server_address1.sin_port = htons(PORT2);
	server_address1.sin_addr.s_addr = INADDR_ANY;
	// inet_aton("192.168.43.66", &server_address.sin_addr);



	//* tryint to connect to the server.
	ret = connect(receiver_fd, (struct sockaddr *)&server_address1, sizeof(server_address1));
		if (ret == -1){ printf("E1: there was an error in making connection to the remote socket.\n"); exit(0);}

	gettimeofday(&(starting_time), NULL);
	protocol5();

	//* sender_fd : I can act as a server and send data to client. [send data and ack]
	//* receiver_fd : I can act as a client and receive data from server. [receive data and ack]
	close(receiver_fd);

	sleep(2);

	close(sender_fd);
	close(server_socket);

	

	return 0;
}




