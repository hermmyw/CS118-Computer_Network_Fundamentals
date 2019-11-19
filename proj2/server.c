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
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
// First, assume there is no packet loss. Just have the client send a packet, and the server respond with an ACK, and so on.

#define PKTSIZE 524
#define HDSIZE 12
#define PLSIZE 512
#define SEQMAX 25600
typedef struct header_proto{
	short seq, ackn;
	char ackf, syn, fin;
    short size;
    short padding;
} header_proto;

void sig_handle() {
	printf("\nServer closed\n");
	exit(0);
}

void print_info(char in, header_proto *header) //DUP !!!
{
	if (in)
		printf("RECV");
	else
		printf("SEND");
	printf(" %d %d 0 0", header->seq, header->ackn);
	if (header->ackf)
		printf(" [ACK]");
	if (header->syn)
		printf(" [SYN]");
	if (header->fin)
		printf(" [FIN]");
	printf("\n");
	return;
}

void test_timeout(int sockfd) {
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


void process(int sockfd, int counter)
{
	int n;
	char pktbuf[PKTSIZE];
	char* plbuf = pktbuf+HDSIZE;
	header_proto* hdin = (header_proto*)pktbuf;
	header_proto* hdout = malloc(HDSIZE*sizeof(char));

	short seqnum = rand() % SEQMAX;
	struct sockaddr cliaddr;
	int len = sizeof(cliaddr);
	while(1)
	{
		int filefd;
		bzero(pktbuf, PKTSIZE);
		int n = recvfrom(sockfd, pktbuf, PKTSIZE, 0, (struct sockaddr*)&cliaddr,&len);
		if (n < 0){
			perror("ERROR reading from socket");
			exit(1);
		}
		print_info(1, hdin);
		if (hdin->syn == 1)
		{
			bzero(hdout, HDSIZE);
			hdout->ackf = 1;
			hdout->ackn = hdin->seq + 1;
			hdout->seq = seqnum;
			hdout->syn = 1;
			n = sendto(sockfd, hdout, HDSIZE, 0, 
          (struct sockaddr*)&cliaddr, sizeof(cliaddr)); 
			if (n < 0){
				perror("ERROR on writing to socket");
				exit(1);
			}
			print_info(0, hdout);
			//open new file first
			char filename[10]; //robustness?!!
			sprintf(filename, "%d.file", counter);
			filefd = open(filename, O_WRONLY|O_CREAT);
		}
		else if (hdin->fin == 1) //closing connection
		{
			bzero(hdout, HDSIZE);
			hdout->ackf = 1;
			hdout->ackn =  hdin->seq + 1;
			hdout->seq = seqnum;
			n = sendto(sockfd, hdout, HDSIZE, 0, 
          (struct sockaddr*)&cliaddr, sizeof(cliaddr)); 
			if (n < 0){
				perror("ERROR on writing to socket");
				exit(1);
			}
			print_info(0, hdout);
			bzero(hdout, HDSIZE);
			hdout->fin = 1;
			hdout->seq = seqnum;
			n = sendto(sockfd, hdout, HDSIZE, 0, 
          (struct sockaddr*)&cliaddr, sizeof(cliaddr)); 
			if (n < 0){
				perror("ERROR on writing to socket");
				exit(1);
			} // need a timer here to wait 2 secs for ACK!!!
			print_info(0, hdout);
			bzero(hdin, HDSIZE);
			n = recvfrom(sockfd, pktbuf, PKTSIZE, 0, (struct sockaddr*)&cliaddr,&len);
			if (n < 0){
				perror("ERROR reading from socket");
				exit(1);
			}
			print_info(1, hdin);
			if (hdin->ackf == 1)
				return;
		}
		else
		{
			if ( hdin->size != 0 &&  hdin->ackf == 1)// &&  hdin->seq == hdout-> + 1) !!!
			{
				n = write(filefd, plbuf,  hdin->size);
				bzero(hdout, HDSIZE);
				hdout->seq =  hdin->ackn;
				hdout->ackf = 1;
				hdout->ackn =  hdin->seq +  hdin->size;
				n = sendto(sockfd, hdout, HDSIZE, 0, 
			(struct sockaddr*)&cliaddr, sizeof(cliaddr)); 
				if (n < 0){
					perror("ERROR on writing to socket");
					exit(1);
				}
				print_info(0, hdout);
			}
			// shall do packet loss implementation here !!!
			// else if ( hdin->ackf == 0) don't know when ?!!
			else
				printf("something unexpected happened");
		}
		seqnum++; //not sure where to put this?!!
	}
}

int stoi(char* str) {
	char* endptr = NULL;
	int ret = (int) strtol(str, &endptr, 10);
	if (endptr == str || ret < 0)
		return -1;
	return ret;
}

int main(int argc, char* argv[])
{

	int sockfd, newsockfd, portno, clilen;
	struct sockaddr_in serv_addr, cli_addr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0){
		perror("ERROR opening socket");
		exit(1);
	}

	if (argc > 1)
		portno = stoi(argv[1]);
	if (portno < 0){
		perror("ERROR invalid port number");
		exit(1);
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portno);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		perror("ERROR on binding");
		exit(1);
	}
	
	signal(SIGQUIT, sig_handle);
	signal(SIGTERM, sig_handle);
	signal(SIGINT, sig_handle);

	int counter = 0;
	while (1)
	{
		counter++;
		process(sockfd, counter);
	}
	close(sockfd);
	return 0;
}
