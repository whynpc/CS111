// UCLA CS 111 Lab 1 command internals

enum command_type
  {
    AND_COMMAND,         // A && B
    SEQUENCE_COMMAND,    // A ; B
    OR_COMMAND,          // A || B
    PIPE_COMMAND,        // A | B
    SIMPLE_COMMAND,      // a simple command
    SUBSHELL_COMMAND,    // ( A )
  };
  

enum token_type
  {
  	INPUT,		//<
  	OUTPUT, 	//>
  	BI_IO,  	//<input >output
  	PIPE,		//|
  	AND, 		//&&
  	OR, 		//||
  	SEMICOLON, 	//; or newline which is equivalent to ;
  	L_BRA, 		//(
  	R_BRA,		//)
  	TOKEN_EMPTY, 		//used for empty stack
  };
//token with priority (higher value means higher priority)
enum token_priority
  {
  	LEVEL_4 = 4, 
  	LEVEL_3 = 3, 
  	LEVEL_2 = 2, 
  	LEVEL_1 = 1,
  	LEVEL_0 = 0,	//for "(" and ")"
  };



// Data associated with a command.
struct command
{
  enum command_type type;

  // Exit status, or -1 if not known (e.g., because it has not exited yet).
  int status;

  // I/O redirections, or null if none.
  char *input;
  char *output;

  union
  {
    // for AND_COMMAND, SEQUENCE_COMMAND, OR_COMMAND, PIPE_COMMAND:
    struct command *command[2];

    // for SIMPLE_COMMAND:
    char **word;

    // for SUBSHELL_COMMAND:
    struct command *subshell_command;
  } u;
};
