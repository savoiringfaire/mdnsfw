#ifndef ARGUMENTS_H
#define ARGUMENTS_H

typedef struct arguments {
	char *peer_ip;
	char *local_ip;
	int peer_port;
	int local_port;
	int silent, verbose;
} arguments;

void parse_arguments(int argc, char *argv[],  arguments *arguments);

#endif