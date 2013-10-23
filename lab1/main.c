// UCLA CS 111 Lab 1 main program

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>

#include "command.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

static char const *program_name;
static char const *script_name;

static void
usage (void)
{
  error (1, 0, "usage: %s [-pt] SCRIPT-FILE", program_name);
}

static int
get_next_byte (void *stream)
{
  return getc (stream);
}

int
main (int argc, char **argv)
{
  int command_number = 1;
  bool print_tree = false;
  bool time_travel = false;
  program_name = argv[0];

  for (;;)
    switch (getopt (argc, argv, "pt"))
      {
      case 'p': print_tree = true; break;
      case 't': time_travel = true; break;
      default: usage (); break;
      case -1: goto options_exhausted;
      }
 options_exhausted:;		//no other options

  // There must be exactly one file argument.
  if (optind != argc - 1)
    usage ();
    
  script_name = argv[optind]; 
  FILE *script_stream = fopen (script_name, "r");
  if (! script_stream)
    error (1, errno, "%s: cannot open", script_name);
  command_stream_t command_stream =
    make_command_stream (get_next_byte, script_stream);

  command_t last_command = NULL;
  command_t command;
  while ((command = read_command_stream (command_stream)))
    {
//		printf("@a\n");
      if (print_tree)
	{
//	printf("@b\n");
	  printf ("# %d\n", command_number++);
	  print_command (command);
//	printf("@c\n");
	}
      else
	{
//	printf("@d\n");
	  last_command = command;
	  execute_command (command, time_travel);
//	printf("@e\n");
	}
    }

  int status;
  waitpid(-1, &status, 0); 
	
  return print_tree || !last_command ? 0 : command_status (last_command);
}
