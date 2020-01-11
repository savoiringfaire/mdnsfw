#include <stdio.h>	//printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <argp.h>
#include <config.h>

#define BUFLEN 1500	//Max length of buffer (set to common MTU of 1500)
#define PORT 5353	//The port on which to listen for incoming data

const char *argp_program_version = PACKAGE_STRING;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;
static char doc[] =
  "a simple mDNS forwarder with peer-to-peer architecture";

static char args_doc[] = "LOCAL_IP PEER_IP";

static struct argp_option options[] = {
  {"verbose",    'v', 0,      0,  "Produce verbose output" },
  {"quiet",      'q', 0,      0,  "Don't produce any output" },
  {"silent",     's', 0,      OPTION_ALIAS },
  {"peerport",   'p', "PORT", 0,  "Listening port of remote peer"},
  {"listenport", 'l', "PORT", 0,  "Listen on this port instead of 8723" },
  { 0 }
};

struct arguments {
	char *peer_ip;
	char *local_ip;
	int peer_port;
	int local_port;
	int silent, verbose;
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = state->input;

	switch (key)
    {
    case 'q': case 's':
		arguments->silent = 1;
		break;
    case 'v':
		arguments->verbose = 1;
		break;
	case 'l':
		arguments->local_port = atoi(arg);
		break;
    case 'p':
		arguments->peer_port = atoi(arg);
		break;

    case ARGP_KEY_ARG:
		if (state->arg_num >= 2)
			/* Too many arguments. */
			argp_usage (state);

		switch (state->arg_num)
		{
		case 0:
			arguments->local_ip = arg;
		case 1:
			arguments->peer_ip = arg;
		}

		break;

    case ARGP_KEY_END:
		if (state->arg_num < 2)
			/* Not enough arguments. */
			argp_usage (state);
		break;

    default:
   	   return ARGP_ERR_UNKNOWN;
    }
	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

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

int main(int argc, char *argv[])
{
	struct arguments arguments;

	arguments.silent = 0;
  	arguments.verbose = 0;
	arguments.peer_port = 8723;
	arguments.local_port = 8723;

	argp_parse (&argp, argc, argv, 0, 0, &arguments);

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

	in_mdns_local.s_addr = inet_addr(arguments.local_ip);
	if(setsockopt(outs, IPPROTO_IP, IP_MULTICAST_IF, (char *)&in_mdns_local, sizeof(in_mdns_local)) < 0)
	{
		die("Error creating outgoing interface");
	}

	char loopch = 0;
	if(setsockopt(outs, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0)
	{
		die("Error turning off loopback on outgoing interface");
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
	si_mdns_me.sin_port = htons(5353);
	si_mdns_me.sin_addr.s_addr = htonl(INADDR_ANY);

	si_mdns_me_group.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
	si_mdns_me_group.imr_interface.s_addr = inet_addr(arguments.local_ip);
	if(setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&si_mdns_me_group, sizeof(si_mdns_me_group)) < 0)
	{
		perror("Adding multicast group error");
		close(s);
		exit(1);
	}

	si_forward_listen.sin_family = AF_INET;
	si_forward_listen.sin_port = htons(arguments.local_port);
	si_forward_listen.sin_addr.s_addr = htonl(INADDR_ANY);

	if((server = gethostbyname(arguments.peer_ip)) == NULL)
	{
		die("gethostbyname");
	}

    si_forwardserver.sin_family = AF_INET; 
    si_forwardserver.sin_port = htons(arguments.peer_port); 
	bcopy((char *)server->h_addr, 
	  (char *)&si_forwardserver.sin_addr.s_addr, server->h_length);
	
	//bind socket to port
	if( bind(s , (struct sockaddr*)&si_mdns_me, sizeof(si_mdns_me) ) == -1)
	{
		die("Error binding to local mdns listener");
	}

	if( bind(peers , (struct sockaddr*)&si_forward_listen, sizeof(si_forward_listen) ) == -1)
	{
		die("Error binding to forwarded mdns listener");
	}

	//keep listening for data
	while(1)
	{
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		FD_SET(peers, &rfds);

		printf("Waiting for data...\n");
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
