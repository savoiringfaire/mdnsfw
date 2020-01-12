#include <config.h>
#include <argp.h>
#include <stdio.h>
#include <arguments.h>
#include <stdlib.h>

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

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
	arguments *arguments = state->input;

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

void parse_arguments(int argc, char *argv[],  arguments *arguments)
{
	arguments->silent = 0;
  	arguments->verbose = 0;
	arguments->peer_port = 8723;
	arguments->local_port = 8723;

	argp_parse (&argp, argc, argv, 0, 0, &arguments);
}