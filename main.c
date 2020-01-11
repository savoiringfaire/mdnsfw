#include<stdio.h>	//printf
#include<string.h> //memset
#include<stdlib.h> //exit(0);
#include<arpa/inet.h>
#include<sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFLEN 1500	//Max length of buffer (set to common MTU of 1500)
#define PORT 5353	//The port on which to listen for incoming data

#define PEER "192.168.1.149"
#define LOCAL_IP "10.20.0.19"
#define PEER_PORT 8723

#define PEER_LISTEN_PORT 8723

struct mdns {
	uint16_t transactionid;
	uint8_t flags_1;
	uint8_t flags_2;
	uint16_t questions;
	uint16_t answers;
	uint16_t authorities;
	uint16_t additional;
} __attribute__ ((__packed__));

void die(char *s)
{
	perror(s);
	exit(1);
}

void enablesockopt(int sockfd, int optname)
{
	int optval = 1;
	if(setsockopt(sockfd, SOL_SOCKET, optname, &optval, sizeof(optval)) == -1)
	{
		die("setsockopt");
	}
}

int main(void)
{
	// Setup mDNS forwarding stuff

	struct in_addr in_mdns_local;
	struct sockaddr_in si_mdns_group;
	socklen_t outs;

	if((outs=socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		die("Error opening outgoing socket");
	}

	memset((char *) &si_mdns_group, 0, sizeof(si_mdns_group));
	si_mdns_group.sin_family = AF_INET;
	si_mdns_group.sin_addr.s_addr = inet_addr("224.0.0.251");
	si_mdns_group.sin_port = htons(5353);

	in_mdns_local.s_addr = inet_addr(LOCAL_IP);
	if(setsockopt(outs, IPPROTO_IP, IP_MULTICAST_IF, (char *)&in_mdns_local, sizeof(in_mdns_local)) < 0)
	{
		die("Setting local interface error");
	}

	char loopch = 0;
	if(setsockopt(outs, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0)
	{
		die("Setting IP_MULTICAST_LOOP error");
		close(outs);
	}

	//
	struct sockaddr_in si_mdns_me, si_mdns_other, si_forwardserver, si_forward_listen, si_forward_listen_other;
	struct ip_mreq si_mdns_me_group;
	struct hostent *server;
	fd_set rfds;
	
	socklen_t s, clients, peers, slen = sizeof(si_mdns_other), recv_len;
	char buf[BUFLEN];
	
	//create a UDP socket
	if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}

	if ((clients=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}

	if ((peers=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}

	enablesockopt(s, SO_REUSEPORT);
	enablesockopt(s, SO_REUSEADDR);
	
	// zero out the structure
	memset((char *) &si_mdns_me, 0, sizeof(si_mdns_me));
	memset((char *) &si_forwardserver, 0, sizeof(si_forwardserver));
	memset((char *) &si_forward_listen, 0, sizeof(si_forward_listen));
	memset((char *) &si_mdns_me_group, 0, sizeof(si_mdns_me_group));
	
	si_mdns_me.sin_family = AF_INET;
	si_mdns_me.sin_port = htons(PORT);
	si_mdns_me.sin_addr.s_addr = htonl(INADDR_ANY);

	si_mdns_me_group.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
	si_mdns_me_group.imr_interface.s_addr = inet_addr(LOCAL_IP);
	if(setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&si_mdns_me_group, sizeof(si_mdns_me_group)) < 0)
	{
		perror("Adding multicast group error");
		close(s);
		exit(1);
	}

	si_forward_listen.sin_family = AF_INET;
	si_forward_listen.sin_port = htons(PEER_LISTEN_PORT);
	si_forward_listen.sin_addr.s_addr = htonl(INADDR_ANY);

	if((server = gethostbyname(PEER)) == NULL)
	{
		die("gethostbyname");
	}

    si_forwardserver.sin_family = AF_INET; 
    si_forwardserver.sin_port = htons(PEER_PORT); 
	bcopy((char *)server->h_addr, 
	  (char *)&si_forwardserver.sin_addr.s_addr, server->h_length);
	
	//bind socket to port
	if( bind(s , (struct sockaddr*)&si_mdns_me, sizeof(si_mdns_me) ) == -1)
	{
		die("bind");
	}

	if( bind(peers , (struct sockaddr*)&si_forward_listen, sizeof(si_forward_listen) ) == -1)
	{
		die("bind");
	}

	//keep listening for data
	while(1)
	{
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		FD_SET(peers, &rfds);

		printf("Waiting for data...\n");
		printf("Peersock: %d\n", peers);
		printf("s:        %d\n", s);
		printf("Larger:   %d\n\n", ((s) >= (peers)) ? (s) : (peers));
		fflush(stdout);

		select(((s) >= (peers)) ? (s) : (peers) + 1, &rfds, NULL, NULL, 0);

		if(FD_ISSET(s, &rfds))
		{
			printf("Got new mdns packet:");

			//try to receive some data, this is a blocking call
			if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_mdns_other, &slen)) == -1)
			{
				die("recvfrom()");
			}

			for(int i = 0; i < recv_len; i++)
			{
				printf("%02X", buf[i]);
			}

			printf("\n");

			sendto(clients, buf, recv_len, MSG_CONFIRM, (struct sockaddr *) &si_forwardserver, sizeof(si_forwardserver));
		}
		else
		{
			if ((recv_len = recvfrom(peers, buf, BUFLEN, 0, (struct sockaddr *) &si_forward_listen_other, &slen)) == -1)
			{
				die("recvfrom()");
			}

			// 224.0.0.251 - multicast address for mDNS


			printf("Got new forwarded packet\n");

			if(sendto(outs, buf, recv_len, 0, (struct sockaddr*)&si_mdns_group, sizeof(si_mdns_group)) < 0)
			{
				perror("Sending datagram message error");
			}

			printf("Relayed forwarded packet\n");
		}
	}

	return 0;
}
