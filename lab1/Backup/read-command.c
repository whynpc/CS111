// UCLA CS 111 Lab 1 command reading

#include "command.h"
#include "command-internals.h"

#include <error.h>

/* FIXME: You may need to add #include directives, macro definitions,
   static function definitions, etc.  */
#include <stdlib.h>
#include <string.h>
#include "alloc.h"

/* FIXME: Define the type 'struct command_stream' here.  This should
   complete the incomplete type declaration in command.h.  */
/////////////////////////////////////////////////////////////////////////////
struct command_node{
	/* command_node: command node for linked list */
	command_t cmd;
	struct command_node* next;
};

struct token_node{
	/* operator_node: operator node for linked list*/
	enum token_type type;
	struct token_node* next;
};

struct command_stream{
	/* command_stream: a container of AND/OR commands
	 * NOTE: even for a single command, we should package it as
	 * a AND/OR command*/
	struct command_node* head;	//must be AND/OR command
};

struct command_stack{
	/* used for holding "values" of the "expression"
	 * We implement stack with linked list
	 * The first element in the list is the stack top
	 * push: create a new element as list head
	 * pop: remove and reset head
	 */
	struct command_node* top;	//stack top(last node)
};

struct token_stack{
	/* used for holding "operators" of the "expression"
	 * We implement stack with linked list
	 * The first element in the list is the stack top
	 * push: create a new element as list head
	 * pop: remove and reset head
	 */
	struct token_node* top; //stack top (last node)
};

//Stack operation
void command_push(struct command_stack* stack, command_t cmd)
{
	//create a new node
	struct command_node* node = (struct command_node*)checked_malloc(sizeof(struct command_node));
	
	node->cmd = cmd;	//shallow copy because command_t is a pointer?
	node->next = stack->top;
	
	stack->top = node;
}

command_t command_pop(struct command_stack* stack)
{
	struct command_node* node = stack->top;
	if(node != NULL)
	{
		stack->top = stack->top->next;
		command_t res = node->cmd;
		free(node);	
		return res;
	}
	else	//stack empty
		return NULL;
	
}

void token_push(struct token_stack* stack, enum token_type type)
{
	//create a new node
	struct token_node* node = (struct token_node*)checked_malloc(sizeof(struct token_node));
	
	node->type = type;	//shallow copy because command_t is a pointer?
	node->next = stack->top;
	
	stack->top = node;
}

enum token_type token_pop(struct token_stack* stack)
{
	struct token_node* node = stack->top;
	if(node != NULL)
	{
		stack->top = stack->top->next;
		enum token_type res = node->type;
		free(node);	
		return res;
	}
	else
		return TOKEN_EMPTY;
}

//get stack top, but do not delete it
enum token_type token_top(struct token_stack* stack)
{
	struct token_node* node = stack->top;
	if(node != NULL)
		return node->type;
	else
		return TOKEN_EMPTY;
}


//Get token priority
enum token_priority GetPriority(enum token_type token)
{
	//I/O redirection > pipeline > AND/OR > newline/;
	switch(token)
	{
		case INPUT: case OUTPUT: case BI_IO:
			return LEVEL_4;break;
		case PIPE:
			return LEVEL_3;break;
		case AND: case OR:
			return LEVEL_2;break;
		case SEMICOLON:
			return LEVEL_1;break;
		default:
			return LEVEL_0;break;
	}
}

//Two Stacks (Globally accessible)
static struct command_stack CmdStack;
static struct token_stack TokenStack;
static struct command_stream CmdStream;
/////////////////////////////////////////////////////////////////////////////
/* exec_token(): apply token to corresponding commands
 * @token: the token 
 * @input, @output: only used for handling I/O redirections
 * return false iff. there are syntax errors
 */
bool 
exec_token(enum token_type token, 
		   const char* input, 
		   const char* output)
{
	switch(token)
	{
		case INPUT:
		{
			if(input==NULL)return false;	//no input path
			
			command_t cmd = command_pop(&CmdStack);
			if(cmd==NULL) return false;	//no sufficient cmds
			//for I/O redirection, just fill in the I/O field in old command
			cmd->input = (char*)input;	//shallow copy?
			cmd->output = NULL;
			command_push(&CmdStack, cmd);
			break;
		}
		case OUTPUT:
		{
			if(output==NULL)return false; //no output path
			command_t cmd = command_pop(&CmdStack);
			if(cmd==NULL) return false; //no sufficient cmds
			//for I/O redirection, just fill in the I/O field in old command
			cmd->input = NULL;	//shallow copy?
			cmd->output = (char*)output;
			command_push(&CmdStack, cmd);
			break;
		}
		case BI_IO:
		{
			if(input==NULL || output==NULL) return false;	//no sufficient paths
			command_t cmd = command_pop(&CmdStack);
			if(cmd==NULL) return false; //no sufficient cmds
			//for I/O redirection, just fill in the I/O field in old command
			cmd->input = (char*)input;	//shallow copy?
			cmd->output = (char*)output;
			command_push(&CmdStack, cmd);
			break;
		}
		case PIPE:
		{
			command_t cmd1 = command_pop(&CmdStack);
			command_t cmd2 = command_pop(&CmdStack);
			if(cmd1==NULL || cmd2==NULL)	//no sufficient commands
				return false;
				
			command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
			if(new_cmd==NULL)return false;
			
			new_cmd->type = PIPE_COMMAND;
			new_cmd->status = 0;
			new_cmd->input = NULL; new_cmd->output = NULL;
			new_cmd->u.command[0] = cmd1;
			new_cmd->u.command[1] = cmd2;
			command_push(&CmdStack, new_cmd);
			break;
		}
		case AND:
		{
			command_t cmd1 = command_pop(&CmdStack);
			command_t cmd2 = command_pop(&CmdStack);
			if(cmd1==NULL || cmd2==NULL)	//no sufficient commands
				return false;
				
			command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
			if(new_cmd==NULL)return false;
			
			new_cmd->type = AND_COMMAND;
			new_cmd->status = 0;
			new_cmd->input = NULL; new_cmd->output = NULL;
			new_cmd->u.command[0] = cmd1;
			new_cmd->u.command[1] = cmd2;
			command_push(&CmdStack, new_cmd);
			break;
		}
		case OR:
		{
			command_t cmd1 = command_pop(&CmdStack);
			command_t cmd2 = command_pop(&CmdStack);
			if(cmd1==NULL || cmd2==NULL)	//no sufficient commands
				return false;
				
			command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
			if(new_cmd==NULL)return false;
			
			new_cmd->type = OR_COMMAND;
			new_cmd->status = 0;
			new_cmd->input = NULL; new_cmd->output = NULL;
			new_cmd->u.command[0] = cmd1;
			new_cmd->u.command[1] = cmd2;
			command_push(&CmdStack, new_cmd);
			break;
		}
		case SEMICOLON:
		{
			//what should I do?
			command_t cmd1 = command_pop(&CmdStack);
			command_t cmd2 = command_pop(&CmdStack);
			//SEMICOLON is special: it accepts NULL commands on both side
			/*if(cmd1==NULL || cmd2==NULL)	//no sufficient commands
				return false;*/
				
			command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
			if(new_cmd==NULL)return false;
			
			new_cmd->type = SEQUENCE_COMMAND;
			new_cmd->status = 0;
			new_cmd->input = NULL; new_cmd->output = NULL;
			new_cmd->u.command[0] = cmd1;
			new_cmd->u.command[1] = cmd2;
			command_push(&CmdStack, new_cmd);
			break;
		}
		default:	//unknown token
			{return false; break;}
	}
	return true;
}

/* on_operator(): used for handling operators
 * @token: the token 
 * @input, @output: only used for handling I/O redirections
 * return false iff. there are syntax errors
 */
bool
on_operator(enum token_type token, 
			const char* input, 
			const char* output)
{
	switch (token)
	{
		case L_BRA:	//"("
		{
			token_push(&TokenStack, token);
			break;
		}
		case R_BRA:	//")"
		{
			enum token_type old_token = token_pop(&TokenStack);
			while(old_token != L_BRA && old_token != TOKEN_EMPTY)
			{
				//recursively pop operator, execute it, and push the result into CmdStack		
				//TODO: CALL exec_token() over old_token
				if(exec_token(old_token, NULL, NULL)) //old_token should not be I/O redirection			
					old_token = token_pop(&TokenStack);
				else	//syntax errors
					return false;
			}
			if(old_token == TOKEN_EMPTY)	//parenthese mismatch
				return false;
			//create a subshell command
			command_t cmd = command_pop(&CmdStack);
			if(cmd==NULL) false;
			command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
			if(new_cmd==NULL)return false;
			
			new_cmd->type = SUBSHELL_COMMAND;
			new_cmd->status = 0;
			new_cmd->input = NULL; new_cmd->output = NULL;
			new_cmd->u.subshell_command = cmd;
			command_push(&CmdStack, new_cmd);
			break;
		}
		default:
		{
			//compare priority, decide whether to push into stack, or pop and execute
			enum token_priority lhs = GetPriority(token);
			enum token_priority rhs = GetPriority(token_top(&TokenStack));
			if(lhs>rhs)
			{
				if(lhs==LEVEL_4)	//I/O redirection
				{
					//TODO: call exec_token() over token
					if(!exec_token(token, input, output))
						return false;		
				}
				else
					token_push(&TokenStack, token);
			}
			else
			{
				enum token_type old_token = token_pop(&TokenStack);
				//TODO: CALL exec_token() over old_token
				if(exec_token(old_token, NULL, NULL))	//old_token should not be I/O redirection
					token_push(&TokenStack, token);
				else	//syntax error
					return false;
			}
			break;
		}
	}
	return true;
}

/* on_simple_cmd(): used for handling simple commands
 * we do not check syntax errors in simple commands, 
 * so this function just applies push operation
 */
bool
on_simple_cmd(const char* cmd)
{
	command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
	if(new_cmd==NULL) return false;
	
	new_cmd->type = SIMPLE_COMMAND;
	new_cmd->status = 0;	//no status so far
	//simple command doesn't have I/O redirection
	new_cmd->input = NULL;
	new_cmd->output = NULL;
	
	//create buffer for command
	new_cmd->u.word = (char**)checked_malloc(strlen(cmd)+1);
	if(new_cmd->u.word==NULL) return false;
	strcpy(*(new_cmd->u.word), cmd);
	
	//push into stack
	command_push(&CmdStack, new_cmd);
	return true;
}

command_stream_t
make_command_stream (int (*get_next_byte) (void *),
		     void *get_next_byte_argument)
{
  /* FIXME: Replace this with your implementation.  You may need to
     add auxiliary functions and otherwise modify the source code.
     You can also use external functions defined in the GNU C Library.  */
     
  //Initialize command and operator stacks
  CmdStack.top = NULL;
  TokenStack.top = NULL;
  CmdStream.head = NULL;
  
  error (1, 0, "make_command_stream: command reading not yet implemented");
  return 0;
}

command_t
read_command_stream (command_stream_t s)
{
  /* FIXME: Replace this with your implementation too.  */
  error (1, 0, "read_command_stream: command reading not yet implemented");
  return 0;
}
