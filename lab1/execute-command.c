// UCLA CS 111 Lab 1 command execution

#include "command.h"
#include "command-internals.h"

#include <error.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>


/* FIXME: You may need to add #include directives, macro definitions,
   static function definitions, etc.  */

int
command_status (command_t c)
{
  return c->status;
}

/* lab 1b: standard execution
 * We apply recursion to execute code in sequence
 * return -1 if error occurs
 */
int 
execute_command_standard(command_t c)
{
	if(c==NULL)
		return -1;
	switch(c->type){
		case AND_COMMAND:{
			if(execute_command_standard(c->u.command[0])!=0)
				return -1;
			if(execute_command_standard(c->u.command[1])!=0)
				return -1;
			return 0;
			break;
		}
		case SEQUENCE_COMMAND:{
			if(execute_command_standard(c->u.command[0])!=0)
				return -1;
			if(execute_command_standard(c->u.command[1])!=0)
				return -1;
			return 0;
			break;
		}
		case OR_COMMAND:{
			int res1 = execute_command_standard(c->u.command[0]);
			int res2 = execute_command_standard(c->u.command[1]);
			if(res1==0 || res2==0)return 0;
			else return -1;
			break;
		}
		case PIPE_COMMAND:{
			/* Take the following steps:
  			 * 1. create a pipe, which has a read/write end
			 * 2. redirect stdin to the read end, and stdout to the write end
			 * 3. execute two commands			
			 */
			int pipefd[2];
			pipe(pipefd);
			pid_t pid = fork();
			if(pid<0)return -1;
			if(pid==0){	//child: execute a in a|b. Only write data
				close(pipefd[0]);	//close read end
				dup2(pipefd[1],STDOUT_FILENO);	//redirect stdout to pipe
				if(execute_command_standard(c->u.command[0])==-1)
					_exit(-1);
				_exit(0);
			}
			else{	//parent: execute b in a|b. Only read data
				close(pipefd[1]);	//close write end
				dup2(pipefd[0],STDIN_FILENO);	//redirect stdin to pipe
				int status;
				waitpid(pid,&status,0);
				if(WEXITSTATUS(status)==-1)return -1;

				if(execute_command_standard(c->u.command[1])==-1)
					return -1;
				return 0;
			}
			break;
		}
		case SIMPLE_COMMAND:{
			//create a new process
			pid_t pid;
			pid = fork();
			if(pid<0)
			{
				return -1;
			}
	
			if(pid==0){		//wait for pid
				//I/O redirection
				int infd, outfd, oldin, oldout;
				int res;
				if(c->input!=NULL)
				{
					infd = open(c->input,O_RDONLY);
					if(infd==-1){printf("read hehe\n");_exit(-1);}
					oldin = dup(STDIN_FILENO);
					dup2(infd, STDIN_FILENO);
					close(infd);
					
				}
				if(c->output!=NULL)
				{
					outfd = open(c->output,O_RDWR|O_CREAT|O_TRUNC);
					if(outfd==-1){printf("write hehe\n");_exit(-1);}
					oldout = dup(STDOUT_FILENO);
					dup2(outfd, STDOUT_FILENO);
					close(outfd);
				}	
				//execute command
				res = execvp(c->u.word[0],c->u.word);
				/*if(c->input!=NULL)
				{
					dup2(oldin,STDIN_FILENO);
					close(oldin);
					
				}
				if(c->output!=NULL)
				{
					dup2(oldout,STDOUT_FILENO);
					close(oldout);
				}*/

				_exit(res);
			}
			else	//parent
			{
				int status;
				waitpid(pid,&status,0);

				return WEXITSTATUS(status);
			}
			break;
		}
		case SUBSHELL_COMMAND:{
			return execute_command_standard(c->u.subshell_command);
			break;	
		}
	}

	return 0;	
}

/*lab 1c: parallel execution*/
int
execute_command_timetravel(command_t c)
{
	//junk code to avoid warnings
	c = c;
	return 0;
}

void
execute_command (command_t c, bool time_travel)
{
  /* FIXME: Replace this with your implementation.  You may need to
     add auxiliary functions and otherwise modify the source code.
     You can also use external functions defined in the GNU C Library.  */
  /* just to avoid warnings */
  if(time_travel)
	execute_command_timetravel(c);
  else
	execute_command_standard(c);
}
