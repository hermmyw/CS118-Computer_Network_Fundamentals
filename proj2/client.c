#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
// First, assume there is no packet loss. Just have the client send a packet, and the server respond with an ACK, and so on.
// correct all exit code!!!

#define PKTSIZE 524
#define HDSIZE 12
#define PLSIZE 512
#define SEQMAX 25600
#define RTO 500
#define WINDOW_SIZE 512
#define THRESHOLD 5120
#define SLOWSTART 1
#define CONGAVOID 2
#define FASTRETR 3

int max(int a, int b) { if (a > b) return a; else return b; }

/* Congestion Control */
int cwnd = WINDOW_SIZE;
int ssthresh = THRESHOLD;
int cc_state = 1;
int acks[3];

typedef struct header_proto{
	short seq, ackn;
	char ackf, syn, fin;
    short size;
    short padding;
} header_proto;

void print_info(char in, header_proto* header, int cwnd, int ssthresh) //cwnd ssthresh DUP !!!
{
	if (in)
		printf("RECV");
	else
		printf("SEND");
	printf(" %d %d", header->seq, header->ackn);
	printf(" %d %d", cwnd, ssthresh);
	if (header->ackf)
		printf(" [ACK]");
	if (header->syn)
		printf(" [SYN]");
	if (header->fin)
		printf(" [FIN]");
	printf("\n");
	return;
}

header_proto* three_way_hs(int sockfd)
{
	header_proto* header=malloc(HDSIZE*sizeof (char));
	short x = rand() % SEQMAX;
	memset(header, 0, HDSIZE);
	header->seq = x;
	header->syn = 1;
	
	int n = write(sockfd, header, HDSIZE);//, 0, NULL, 0);
	if (n < 0){
		perror("ERROR on writing to socket");
		exit(1);
	}
	print_info(0, header, cwnd, ssthresh);
	bzero(header, HDSIZE);
	n = read(sockfd, header, HDSIZE);
	if (n < 0) {
		perror("ERROR reading from socket");
		exit(1);
	}
	print_info(1, header, cwnd, ssthresh);
	if (header->ackf == 1 && header->syn == 1 && header->ackn == x+1)
		return header;
	else{
		fprintf(stderr, "ERROR: unable to complete 3-way handshake\n");
		exit(1);
	}
}

void client_timeout(int sockfd) {
	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0; // wait for 10 seconds
	int received = 0;
	fd_set inSet;
	FD_ZERO(&inSet);
	FD_SET(sockfd, &inSet);
	received = select(sockfd+1, &inSet, NULL, NULL, &timeout);
	printf(">>> Check timeout: %d\n", received);
	if (received != 1) {
		printf("Time out on reading from socket. \n");
		close(sockfd);
		exit(1);
	}
}

void retrans_timeout(int sockfd) {
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = RTO; // wait for 0.5 seconds
	int received = 0;
	fd_set inSet;
	FD_ZERO(&inSet);
	FD_SET(sockfd, &inSet);
	received = select(sockfd+1, &inSet, NULL, NULL, &timeout);
	printf(">>> Check timeout: %d\n", received);
	if (received != 1) {
		printf("Time out and start retransmission. \n");
		ssthresh = max(cwnd/2, 1024);
		cwnd = PLSIZE;
		// retransmit data after the last ACK byte
		cc_state = SLOWSTART;
	}
}

int last_three_ack(int ackn) {
	printf("Received ACK: %d\n", ackn);
	acks[0] = acks[1];
	acks[1] = acks[2];
	acks[2] = ackn;
	if (acks[0] == acks[1] && acks[1] == acks[2]) {
		printf("Three duplicate acks: %d\n", acks[0]);
		return 1;
	}
	return 0;
}


void sendfile(int sockfd, int input_file)
{
	// prep
	int n, dupcount=0;
	char pktbuf[PKTSIZE];
	header_proto* hdout = (header_proto*)pktbuf;
	header_proto* hdin = malloc(HDSIZE*sizeof(char));
	memset(hdin, 0, HDSIZE);
	memset(pktbuf, 0, PKTSIZE);
	struct timeval start_time;
	struct timeval end_time;

	// handshake
	header_proto* temp = three_way_hs(sockfd);
	printf(">>> Finish three way handshake\n");
	*hdin = *temp;
	
	// sendfile
	short seqnum = hdin->ackn; //window size!!!

	while (1)
	{
		// store how many bytes were actually read in bytes_read each time
		bzero(pktbuf, PKTSIZE);
		hdout->seq = seqnum;
		hdout->ackf = 1;
		hdout->ackn = hdin->seq + 1;
		int bytes_read = read(input_file, pktbuf + 12, PLSIZE);
		printf(">>> Finish reading from input\n");
		if (bytes_read == 0) // We're done reading from the file
		{
			bzero(hdout, HDSIZE);
			hdout->seq = seqnum;
			hdout->fin = 1;
			n = write(sockfd, hdout, HDSIZE);
			if (n < 0){
				perror("ERROR on writing to socket");
				close(sockfd);
				exit(1);
			}
			print_info(0, hdout, cwnd, ssthresh);
			while(1) // Waiting for server's ACK
			{
				bzero(hdin, HDSIZE);

				client_timeout(sockfd);
				n = read(sockfd, hdin, HDSIZE); // timer here!!!
				if (n < 0) {
					perror("ERROR reading from socket");
					exit(1);
				}
				print_info(1, hdin, cwnd, ssthresh); // slide window upon receiving ACK
				if (last_three_ack(hdin->ackn)) {
					ssthresh = max(cwnd/2, 1024);
					cwnd = ssthresh + 1536;
					// retransmit
					bzero(hdout, HDSIZE);
					hdout->seq = seqnum;
					hdout->fin = 1;
					n = write(sockfd, hdout, HDSIZE);
					if (n < 0){
						perror("ERROR on writing to socket");
						close(sockfd);
						exit(1);
					}
					print_info(0, hdout, cwnd, ssthresh);

					cwnd = ssthresh;
					cc_state = CONGAVOID;
				}

				if (cc_state == SLOWSTART) {
					if (cwnd < ssthresh)
						cwnd += 512;
					else
						cc_state = CONGAVOID;
				}
				else if (cc_state == CONGAVOID) {
					if (cwnd >= ssthresh)
						cwnd += (512*512)/cwnd;
				}
				if (hdin->ackf == 1 && hdin->ackn == seqnum+1)
				{
					bzero(hdin, HDSIZE);

					client_timeout(sockfd);
					n = read(sockfd, hdin, HDSIZE); // timer here!!!
					if (n < 0) {
						perror("ERROR reading from socket");
						exit(1);
					}
					print_info(1, hdin, cwnd, ssthresh);
					if (hdin->fin == 1)
					{
						bzero(hdout, HDSIZE);
						hdout->seq = seqnum + 1;
						hdout->ackf = 1;
						hdout->ackn = hdin->seq + 1;
						n = write(sockfd, hdout, HDSIZE);
						if (n < 0){
							perror("ERROR on writing to socket");
							close(sockfd);
							exit(1);
						}
						print_info(0, hdout, cwnd, ssthresh);
						close(sockfd);
						close(input_file);
						exit(0);
					}
				}
				else
					continue;
			}
		}
		if (bytes_read < 0){
			perror("ERROR on read");
			close(input_file);
			exit(1);
		}
		hdout->size = bytes_read;
		seqnum+=bytes_read;
		if (seqnum >= SEQMAX)
			seqnum-=SEQMAX;

		// not all of the data may be written in one call; write will return how many bytes were written
		void *p = pktbuf;
		while (bytes_read > 0)
		{
			int bytes_written = write(sockfd, p, (hdout->size)+HDSIZE);
			if (bytes_written <= 0){
				perror("ERROR on write");
				close(input_file);
				exit(1);
			}
			print_info(0, hdout, cwnd, ssthresh);
			bytes_read -= bytes_written;
			p += bytes_written;
		}

		/* Now read server response */
		// need a timer here!!!
		bzero(hdin, HDSIZE);
		client_timeout(sockfd);
		n = read(sockfd, hdin, HDSIZE);
		if (n < 0) {
			perror("ERROR reading from socket");
			exit(1);
		}
		print_info(1, hdin, cwnd, ssthresh);
		if (hdin->ackf < 0)
			printf("unexpected 0 ackf");
		if (hdin->ackn < seqnum)
			dupcount++; // maybe extra credit?!!
	}
	close(sockfd);
	if (input_file)
		close(input_file);
}

int stoi(char *str)
{
	char *endptr = NULL;
	int ret = (int)strtol(str, &endptr, 10);
	if (endptr == str || ret < 0)
		return -1;
	return ret;
}






int main(int argc, char *argv[])
{
	int sockfd, portno, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	if (argc < 4)
	{
		fprintf(stderr, "usage %s hostname port filename\n", argv[0]);
		exit(0);
	}

	portno = atoi(argv[2]);	

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	{
		perror("ERROR: opening socket");
		exit(1);
	}

	server = gethostbyname(argv[1]);

	if (server == NULL || portno == 0)
	{
		fprintf(stderr, "ERROR: unable to find host or no such host\n");
		exit(1);
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(portno);

	/* Now connect to the server */
	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR: connection failed");
		exit(1);
	}

	int filefd = open(argv[3], O_RDONLY);

	sendfile(sockfd, filefd);
	return 0;
}
