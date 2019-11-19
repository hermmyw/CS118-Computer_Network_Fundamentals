//
//  client.c
//  Project_2
//
//  Created by Tom Zhang on 3/5/18.
//  Copyright © 2018 CS_118. All rights reserved.
//

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdlib.h>

#define MTU 1024
#define HEADERSIZE 52
#define DATASIZE 972
#define WAIT 500

struct packet_info
{
    //Packet Types:
    //Datagram: 1
    //ACK: 2
    //Retransmission: 3
    
    int type;
    int seq_no;
    int max_no;
    
    //Fin:
    //0: Middle of file
    //1: Finished
    
    int fin;
    int error;
    double time;
    char data[DATASIZE];
    int data_size;
    int seq_count;
};


int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int bytesrv;

    if (argc != 4)
    {
        fprintf(stderr,"usage: client <server_hostname> <server_portnumber> <filename>\n");
        exit(1);
    }
    
    char *port = argv[2];
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    if ((rv = getaddrinfo(argv[1], port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("receiver: socket");
            continue;
        }
        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "Error: receiver failed to bind socket\n");
        return 2;
    }
    
    if ((bytesrv = sendto(sockfd, argv[3], strlen(argv[3]), 0, p->ai_addr, p->ai_addrlen)) == -1)
    {
        perror("Error: failed to send filename\n");
        exit(1);
    }
    
    //freeaddrinfo(servinfo);
    
    printf("Requested %s as received.data from %s\n", argv[3], argv[1]);
    
    printf("Waiting for sender's file...\n");
    
    char newFilename[MTU];
    strcpy(newFilename, "received.data");
    
    FILE *fp = fopen(newFilename, "w+");
    
    if (fp==NULL)
    {
        printf("Error: file cannot be created!\n");
        exit(1);
    }
    
    
    int window_size = 5;
    struct packet_info response_packet;
    memset((char *) &response_packet, 0, sizeof(response_packet));
    
    response_packet.fin = 0;
    response_packet.seq_no = 0;
    
    struct packet_info *buffer;
    buffer = (struct packet_info *) malloc(window_size * sizeof(struct packet_info));
    int i;
    for (i = 0; i<window_size; i++)
        memset(&(buffer[i]), -1, sizeof(struct packet_info));
    
    int start_seq = 0;
    int end_seq = window_size;
    char* status = "";
    while(1)
    {
        recvfrom(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, &(p->ai_addrlen));
        
        if (response_packet.error == 1) {
            printf("Requested file not found!\n");
            return 0;
        }
        
        //Send back ACK if this is SYN packet
        if (response_packet.seq_no == 0 && response_packet.fin == 2) {
            printf("Receiving packet 0\n");
            sendto(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet 0 SYN\n");
            continue;
        }
        
        if (response_packet.fin == 1)
            status = "FIN";
        else if (response_packet.type == 3)
            status = "Retransmission";
        else if (response_packet.fin == 2)
            status = "SYN";
        else
            status = "";
        
        fprintf(stdout, "Receiving packet %d %s\n", response_packet.seq_no, status);
        
           if (end_seq > response_packet.max_no)
            end_seq = response_packet.max_no;
        else
            end_seq = window_size;
        
        
        int current_pkt = (response_packet.seq_no + response_packet.seq_count*30720) / DATASIZE;
         if ((current_pkt - start_seq) >= 0
            && (current_pkt - start_seq) < window_size){
            // send ACK
            struct packet_info ack_packet;
            memset((char *) &ack_packet, 0, sizeof(ack_packet));
            ack_packet.type = 2;
            ack_packet.seq_no = response_packet.seq_no;
            ack_packet.seq_count = response_packet.seq_count;
            sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet %d %s\n", ack_packet.seq_no, status);
            
            // buffer first
            memcpy(&(buffer[current_pkt - start_seq]), &response_packet, sizeof(struct packet_info));
            
            while (1){
                
                if ((buffer[0].seq_no+buffer[0].seq_count*30720) / DATASIZE == start_seq) {
                    fwrite(buffer[0].data, sizeof(char), buffer[0].data_size, fp);
                    
                    for (i = 0; i<window_size-1; i++){
                        //shift the buffer to the left
                        memcpy(&(buffer[i]), &(buffer[i+1]), sizeof(struct packet_info));
                    }
                    memset(&(buffer[window_size-1]), -1, sizeof(struct packet_info));
                    start_seq++;
                }
                else
                    break;
            }
        }
        else if ((current_pkt - start_seq) >= -window_size
                 && (current_pkt - start_seq) < 0){
            response_packet.type = 2;
            sendto(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet %d Retransmission\n", response_packet.seq_no);
        }
        
        
        memset((char *) &response_packet.data, 0, sizeof(response_packet.data));
        
        if (response_packet.fin == 1) {
            printf("Transmission done.\nClosing connection...\n");
            struct packet_info fin_packet;
            memset((char *) &fin_packet, 0, sizeof(fin_packet));
            fin_packet.fin = 3;
            fd_set inSet;
            struct timeval timeout;
            
            FD_ZERO(&inSet);
            FD_SET(sockfd, &inSet);
            
            // wait for specified time
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            
            int retry = 0;
            while (1) {
                retry++;
                sendto(sockfd, &fin_packet, sizeof(fin_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
                if (retry > 4) {
                    close(sockfd);
                    return 0;
                }
            }
        }
    }
    
    return 0;
}