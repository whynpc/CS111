// UCLA CS 111 Lab 1 command reading

#include "command.h"
#include "command-internals.h"

#include <error.h>

/* FIXME: You may need to add #include directives, macro definitions,
   static function definitions, etc.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
	/* token_node: token node for linked list*/
	enum token_type type;
	struct token_node* next;
};

struct word_node{
	/* word_node: for word_stack*/
	char c;
	struct word_node* next;
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

struct word_stack{
	/*word stack: used for holding simple commands' words*/
	struct word_node* top;
	unsigned int len;	//word length
	bool in_word;		//true if at least one word
};

//Stack operation
void word_push(struct word_stack* stack, char c)
{
	struct word_node* node = (struct word_node*)checked_malloc(sizeof(struct word_node));
	node->c = c;
	node->next = stack->top;
	stack->top = node;
	stack->len++;
}

char word_pop(struct word_stack* stack)
{
	struct word_node* node = stack->top;
	if(node != NULL)
	{
		stack->top = node->next;
		char c = node->c;
		free(node);
		stack->len--;
		return c;	
	}
	else
		return '\0';
}

void word_free(struct word_stack* stack)
{
	struct word_node* p;
	while(stack->top != NULL)
	{
		p = stack->top;
		stack->top = stack->top->next;
		free(p);
	}	
}

//convert from word stack to a word buffer
//reset word stack

char* create_buf(struct word_stack* stack)
{
	if (!stack->in_word) {
		return NULL;
	}
	char* res = (char*)checked_malloc(stack->len+1);
	int u=stack->len;
	res[u]='\0';
	while(stack->top!=NULL)
		res[--u] = word_pop(stack);
	//reset stack
	stack->top = NULL;
	stack->len = 0;
	stack->in_word = false;

	return res;
}


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
		//free(node);	
		return res;
	}
	else	//stack empty
		return NULL;

}

//free command_t
void free_command_t(command_t cmd)
{
	if(cmd==NULL) return;

	if(cmd->input != NULL)
	{free(cmd->input); cmd->input = NULL;}
	if(cmd->output != NULL)
	{free(cmd->output); cmd->output = NULL;}
	switch(cmd->type)
	{

		case SIMPLE_COMMAND:
			{
				if(cmd->u.word != NULL)
				{free(cmd->u.word);	cmd->u.word = NULL;}//TAKE CARE OF char**
				break;
			}
		case AND_COMMAND:
		case OR_COMMAND:
		case PIPE_COMMAND:
		case SEQUENCE_COMMAND:
			{
				if(cmd->u.command[0] != NULL)
				{free(cmd->u.command[0]); cmd->u.command[0] = NULL;}
				if(cmd->u.command[1] != NULL)
				{free(cmd->u.command[1]); cmd->u.command[1] = NULL;}
				break;
			}
		case SUBSHELL_COMMAND:	//recursively free the command
			{
				free_command_t(cmd->u.subshell_command);
				break;
			}
	}
}
//free stack in case of syntax errors
void command_free(struct command_stack *stack)
{
	struct command_node* p;
	while(stack->top != NULL)
	{
		p = stack->top;
		stack->top = stack->top->next;
		//free node p
		//free I/O redirection
		free_command_t(p->cmd);
		free(p);	
	}
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

//free token stack
void token_free(struct token_stack* stack)
{
	struct token_node* p;
	while(stack->top != NULL)
	{
		p = stack->top;
		stack->top = stack->top->next;
		free(p);
	}
}


//Get token priority
enum token_priority GetPriority(enum token_type token)
{
	//I/O redirection > pipeline > AND/OR > newline/;
	switch(token)
	{
		case INPUT: case OUTPUT: 
			return LEVEL_4;break;
		case PIPE:
			return LEVEL_3;break;
		case AND: case OR:
			return LEVEL_2;break;
		case SEMICOLON: case NEWLINE:
			return LEVEL_1;break;
		default:
			return LEVEL_0;break;
	}
}

//Stacks (Globally accessible)
static struct command_stack CmdStack;
static struct token_stack TokenStack;
static struct word_stack WordStack;
static struct command_stream CmdStream;
/////////////////////////////////////////////////////////////////////////////
//decide whether c is a word
bool isword(char c)
{
	return (isalnum(c)||c=='!'||c=='%'||c=='+'||c=='-'||c=='/'||c==':'||c=='@'||c=='^'||c=='_'||c=='.');	
}
//decide whether c is a whitespace/tab
bool iswhitespace(char c)
{
	return c==' '||c=='	';
}
//Syntax error
void on_syntax(int line_count)
{
	printf("line %d errors", line_count);
	//token_free(&TokenStack);
	//word_free(&WordStack);
	//command_free(&CmdStack);
	exit(-1);
}
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
				if(input==NULL)return false;//no input path

				command_t cmd = command_pop(&CmdStack);
				if(cmd==NULL) return false;//no sufficient cmds
				//for I/O redirection, just fill in the I/O field in old command
				if(cmd->input == NULL)//input should not be assigned yet
					cmd->input = (char*)input;//shallow copy?
				else//redundant input, should be a syntax error
					return false;
				if(cmd->output != NULL)//output should not be assigned before input
					return false;
				command_push(&CmdStack, cmd);
				break;
			}
		case OUTPUT:
			{
				if(output==NULL)return false; //no output path
				command_t cmd = command_pop(&CmdStack);
				if(cmd==NULL) return false; //no sufficient cmds
				//for I/O redirection, just fill in the I/O field in old command
				//we don't need to check input here. It can be either filled or not
				if(cmd->output==NULL)//output should not be assigned yet
					cmd->output = (char*)output;//shallow copy?
				else//redundant output, should be a syntax error
					return false;
				command_push(&CmdStack, cmd);
				break;
			}
		case PIPE:
			{
				command_t cmd1 = command_pop(&CmdStack);
				command_t cmd2 = command_pop(&CmdStack);
				if(cmd1==NULL || cmd2==NULL)//no sufficient commands
					return false;

				command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
				if(new_cmd==NULL)return false;

				new_cmd->type = PIPE_COMMAND;
				new_cmd->status = 0;
				new_cmd->input = NULL; new_cmd->output = NULL;
				new_cmd->u.command[0] = cmd2;
				new_cmd->u.command[1] = cmd1;
				command_push(&CmdStack, new_cmd);
				break;
			}
		case AND:
			{
				command_t cmd1 = command_pop(&CmdStack);
				command_t cmd2 = command_pop(&CmdStack);
				if(cmd1==NULL || cmd2==NULL)//no sufficient commands
					return false;

				command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
				if(new_cmd==NULL)return false;

				new_cmd->type = AND_COMMAND;
				new_cmd->status = 0;
				new_cmd->input = NULL; new_cmd->output = NULL;
				new_cmd->u.command[0] = cmd2;
				new_cmd->u.command[1] = cmd1;
				command_push(&CmdStack, new_cmd);
				break;
			}
		case OR:
			{
				command_t cmd1 = command_pop(&CmdStack);
				command_t cmd2 = command_pop(&CmdStack);
				if(cmd1==NULL || cmd2==NULL)//no sufficient commands
					return false;

				command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
				if(new_cmd==NULL)return false;

				new_cmd->type = OR_COMMAND;
				new_cmd->status = 0;
				new_cmd->input = NULL; new_cmd->output = NULL;
				new_cmd->u.command[0] = cmd2;
				new_cmd->u.command[1] = cmd1;
				command_push(&CmdStack, new_cmd);
				break;
			}
		case SEMICOLON:
			{
				//what should I do?
				command_t cmd2 = command_pop(&CmdStack);
				command_t cmd1 = command_pop(&CmdStack);
				//SEMICOLON is special: it accepts NULL commands on right side
				if(cmd1==NULL)//no sufficient commands 
				{
					printf("no left child for semicolon\n");
					return false;
				}					


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
		case NEWLINE:
			{
				break;

			}
		default://unknown token
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
on_token(enum token_type token, 
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
				if(cmd==NULL) return false;
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
				printf("token: %d\n", token);
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
					{	
						if (token != NEWLINE)
							token_push(&TokenStack, token);

					}
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
on_simple_cmd(char* cmd)
{
	if (!cmd) {
		return true;

	}
	printf("on simple command: %s\n", cmd);
	command_t new_cmd = (command_t)checked_malloc(sizeof(struct command));
	if(new_cmd==NULL) return false;

	new_cmd->type = SIMPLE_COMMAND;
	new_cmd->status = 0;	//no status so far
	//simple command doesn't have I/O redirection
	new_cmd->input = NULL;
	new_cmd->output = NULL;

	int word_cnt = 0;
	bool inword = false;
	char *c = cmd;
	while (*c) {
		if (inword) {
			if (iswhitespace(*c)) {
				inword = false;
			}		
		} else {
			if (isword(*c)) {				
				inword = true;
				word_cnt ++;
			}
		}
		c ++;
	}
	//printf("word cnt: %d\n", word_cnt);
	//create buffer for command
	char **wordbuf = (char **)checked_malloc(sizeof(char *) * word_cnt + 1);
	memset(wordbuf, 0, sizeof(char *) * word_cnt);

	int wordbuf_cnt = 0;
	char *i, *j;
	i = cmd;
	do {
		while (!isword(*i)) {
			i ++;
		}
		j = i;
		while (isword(*j) && *j) {
			j ++;
		}
		char *word = (char *)checked_malloc(j - i + 1);
		memset(word, 0, j - i + 1);
		strncpy(word, i, j - i);
		wordbuf[wordbuf_cnt] = word;

		i = j;
		wordbuf_cnt ++;
	} while(wordbuf_cnt < word_cnt);
	//char *nullword = (char *)checked_malloc(1);
	//memset(nullword, 0, 1);
	wordbuf[word_cnt] = NULL;


	int k;
	for (k = 0; k < wordbuf_cnt; k ++) {
	//	printf("word #%d: %s\n", k, wordbuf[k]);
	}
	new_cmd ->u.word = wordbuf;
	//strcpy(wordbuf, cmd);
	//new_cmd->u.word = &wordbuf;
	//new_cmd->u.word = (char**)checked_malloc(strlen(cmd)+1);
	//if(new_cmd->u.word==NULL) return false;
	//strcpy(*(new_cmd->u.word), cmd);
	//	new_cmd->u.word = &cmd;
	//


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
	WordStack.top = NULL;
	WordStack.len = 0;
	WordStack.in_word = false;
	CmdStream.head = NULL;

	bool hold_on = false;	//true if no need to get a new char
	bool exit_loop = false;
	char c;
	unsigned int line_count = 0;	//line count

	while(true)
	{
		if(hold_on) hold_on=false;	//don't read a new word
		else c = get_next_byte(get_next_byte_argument);
		//printf("read char %c\n", c);	
		switch(c)
		{
			case '(': 
				{
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(L_BRA,NULL,NULL))
						on_syntax(line_count);
					break;
				}
			case ')':
				{
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(R_BRA,NULL,NULL))
						on_syntax(line_count);
					break;
				}
			case '&':
				{
					c = get_next_byte(get_next_byte_argument);
					if(c!='&') 	
						on_syntax(line_count);
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(AND,NULL,NULL))
						on_syntax(line_count);
					break;
				}
			case '|':
				{
					c = get_next_byte(get_next_byte_argument);
					if(c=='|')	//OR
					{
						if(!on_simple_cmd(create_buf(&WordStack)))
							on_syntax(line_count);
						if(!on_token(OR,NULL,NULL))
							on_syntax(line_count);
					}
					else	//PIPE
					{
						hold_on = true;
						if(!on_simple_cmd(create_buf(&WordStack)))
							on_syntax(line_count);
						if(!on_token(PIPE,NULL,NULL))
							on_syntax(line_count);

					}
					break;
				}
			case ';':
				{
					//a simple command should appear before ';'
					if(WordStack.len==0)
						on_syntax(line_count);
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(SEMICOLON,NULL,NULL))
						on_syntax(line_count);
					break;
				}
			case '#':	//comment
				{	
					c = get_next_byte(get_next_byte_argument);
					while(c!='\r' && c!='\n') {
						//printf("%c", c);
						c = get_next_byte(get_next_byte_argument);

					}
					hold_on = true;
					break;
				}
			case '\r': case'\n':	//newline
				{
					bool comment = false;
					while (1) {
						c = get_next_byte(get_next_byte_argument);
						if (c == EOF) {
							break;
						} else if (c == '\r' ||  c == '\n') {
							comment = false;
							continue;
						} else if (iswhitespace(c)) {
							continue;
						} else if (c == '#') {
							printf("meet #\n");
							comment = true;
						} else if (comment) {
							continue;
						} else {
							break;
						}

					}

					if(c=='(' || c==')' || isword(c))
					{
						//if(WordStack.len!=0)
						if (1)
						{
							int buflen = WordStack.len;
							hold_on = true;
							
							if(!on_simple_cmd(create_buf(&WordStack)))
								on_syntax(line_count);
							if (buflen != 0) {
								printf("on token newline\n");
								if(!on_token(NEWLINE,NULL,NULL))
									on_syntax(line_count);
							} else {
								printf("not on token newline\n");

							}
						}
						else
							on_syntax(line_count);
					} else if (c == EOF) {
						hold_on = true;
					}
					else
						on_syntax(line_count);
					break;
				}
			case '<': //I/O redirection
				{
					struct word_stack input;
					input.top = NULL;
					input.len = 0;
					input.in_word = false;
					//skip unnecessary spaces
					while(iswhitespace(c=get_next_byte(get_next_byte_argument)))
					{
						//DO NOTHING
						
					}

					if(!isword(c))
						on_syntax(line_count);
					//find input
					input.in_word = true;
					do{
						word_push(&input, c);
						c=get_next_byte(get_next_byte_argument);
					}while(isword(c));
					if (c != '\r' && c !='\n')
						hold_on = true;
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(INPUT,create_buf(&input),NULL))
						on_syntax(line_count);		
					break;
				}
			case '>':
				{
					struct word_stack output;
					output.top = NULL;
					output.len = 0;
					output.in_word = false;
					//skip unnecessary spaces
					while(iswhitespace(c=get_next_byte(get_next_byte_argument)))
					{
						//DO NOTHING
					}
					if(!isword(c))
						on_syntax(line_count);
					//find input
					output.in_word = true;
					do{
						word_push(&output, c);
						c=get_next_byte(get_next_byte_argument);
					}while(isword(c));
					if (c != '\r' && c != '\n')
						hold_on = true;
					if(!on_simple_cmd(create_buf(&WordStack)))
						on_syntax(line_count);
					if(!on_token(OUTPUT, NULL, create_buf(&output)))
						on_syntax(line_count);		
					break;
				}
			case EOF:
				{
					if (WordStack.len != 0) 
					{
						if(!on_simple_cmd(create_buf(&WordStack)))
							on_syntax(line_count);
					}
					//TODO: recursively pop token and exec it
					enum token_type old_token = token_pop(&TokenStack);
					while(old_token != TOKEN_EMPTY)
					{
						//recursively pop operator, execute it, and push the result into CmdStack		
						//TODO: CALL exec_token() over old_token
						//printf("token: %d\n", old_token);
						if(exec_token(old_token, NULL, NULL)) //old_token should not be I/O redirection			
							old_token = token_pop(&TokenStack);
						else	//syntax errors
							on_syntax(line_count);	//line_count may not be meaningful here
					}
					if (CmdStack.top == NULL) {
						on_syntax(line_count);
					}
					//if(CmdStack.top->next!=NULL) {
					//	on_syntax(line_count);
					//}
					exit_loop = true;
					break;
				}
			default:
				{
					if(isword(c)||(iswhitespace(c) && WordStack.in_word))
					{
						word_push(&WordStack, c);
						if(isword(c))
							WordStack.in_word = true;
					}
					break;
				}
		}
		if(exit_loop)	//EOF
			break;
	} // end of while
	printf("outside while\n");

	while(CmdStack.top!=NULL)
	{
		command_t cmd = command_pop(&CmdStack);
		struct command_node* p = (struct command_node*)checked_malloc(sizeof(struct command_node));
		p->cmd = cmd;
		p->next = CmdStream.head;
		CmdStream.head = p;
	}

	//error (1, 0, "make_command_stream: command reading not yet implemented");
	return &CmdStream;
}

	command_t
read_command_stream (command_stream_t s)
{
	/* FIXME: Replace this with your implementation too.  */
	//printf("read command stream\n");
	if (!(s->head))
		return NULL;
	struct command_node* p = s->head;
	s->head = p->next;
	command_t res = p->cmd;
	//free(p);
	return res;
	//error (1, 0, "read_command_stream: command reading not yet implemented");
	//return 0;
}
