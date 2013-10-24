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
#include <string.h>

struct file_usage {
  char *file_name;
  pid_t pid;
  struct file_usage *next;
};
typedef struct file_usage* file_usage_t;

struct file_usage_list {
  file_usage_t head;
  file_usage_t tail;
};
typedef struct file_usage_list* file_usage_list_t;

file_usage_list_t file_usage_stat_all;

static file_usage_t
retrieve_file_usage(file_usage_list_t l, char *file_name) 
{
  if (file_name == NULL)
    return NULL;

  file_usage_t p = l->head;
  while (p)
    {
      if (strcmp(p->file_name, file_name))
	{
	  return p;
	}
      p = p->next;
    }
  return p;
}


/* Called when there is a new command depending on a file used by previous commands */
static void
update_file_usage(file_usage_t p, pid_t pid)
{
  if (p && pid > 0)
    {
      /* we only record the latest process of executing command 
	 depending on the file; later commands depending on the
	 same file have to wait for the latest command */
      p->pid = pid;
    }
}

/* Called only if the file never used by previous command; 
   if the file already used by previous command, call update_file_usage() */
static void
add_file_usage(file_usage_list_t l, char *file_name, pid_t pid)
{
  file_usage_t new_node = (file_usage_t) malloc(sizeof(struct file_usage));
  new_node->file_name = (char *) malloc(strlen(file_name) + 1);
  strcpy(new_node->file_name, file_name);
  new_node->pid = pid;
  new_node->next = NULL;

  if (l->head && l->tail)
    {
      l->tail->next = new_node;
      l->tail = new_node;
    }
  else
    {
      l->head = new_node;
      l->tail = new_node;
    }
}

/* you always want to use this function to add file usage */
static void
try_add_file_usage(file_usage_list_t l, char *file_name, pid_t pid)
{
  file_usage_t p = retrieve_file_usage(l, file_name);
  if (p)
    {
      update_file_usage(p, pid);
    }
  else
    {
      add_file_usage(l, file_name, pid);
    }
}

static file_usage_list_t
make_file_usage_list()
{
  file_usage_list_t l;
  l = (file_usage_list_t) malloc(sizeof(struct file_usage_list));
  return l;
}

int
command_status (command_t c)
{
  return c->status;
}

static void
redirect_input (char *input)
{
  if (input) 
    {
      int infd;
      infd = open(input,O_RDONLY);
      if(infd==-1){printf("read hehe\n");_exit(-1);}
      dup2(infd, STDIN_FILENO);
      close(infd);
    }
}

static void
redirect_output (char *output)
{
  if (output)
    {
      int outfd;
      outfd = open(output,O_RDWR|O_CREAT|O_TRUNC, 0644);
      if(outfd==-1){printf("write hehe\n");_exit(-1);}
      dup2(outfd, STDOUT_FILENO);
      close(outfd);
    }
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
    pid_t pid;
    while ((pid = fork()) < 0);
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
    while ((pid = fork()) < 0);
    if(pid<0)
      {
	return -1;
      }
	
    if(pid==0){		//wait for pid
      //I/O redirection

      int res;
      redirect_input(c->input);
      redirect_output(c->output);

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
    redirect_input(c->input);
    redirect_output(c->output);
    return execute_command_standard(c->u.subshell_command);
    break;	
  }
  }

  return 0;	
}


static void
check_single_file_dependency(char *file_name, file_usage_list_t l)
{
  /* no iio redirection */
  if (file_name == NULL)
    return;
  /*  file aleady taken as the dependency of current command */
  if (retrieve_file_usage(l, file_name))
    return;

  file_usage_t fu = retrieve_file_usage(file_usage_stat_all, file_name);
  if (fu)
    {
      add_file_usage(l, fu->file_name, fu->pid);
    }
  else
    {
      /* if a file not used by previous commands,
         we still save it into dependency list with pid=0, 
         indicating current command first use the file*/
      add_file_usage(l, fu->file_name, 0);
    }
}

/* check the command depends on which commands;
   save results in l */
static void
check_command_file_dependency(command_t c, file_usage_list_t l)
{
  if (c == NULL)
    return;

  switch (c->type)
    {
    case AND_COMMAND:
    case OR_COMMAND:
    case SEQUENCE_COMMAND:
    case PIPE_COMMAND:
      check_command_file_dependency(c->u.command[0], l);
      check_command_file_dependency(c->u.command[1], l);
      break;
    case SIMPLE_COMMAND:
      check_single_file_dependency(c->input, l);
      check_single_file_dependency(c->output, l);
      break;
    case SUBSHELL_COMMAND:
      check_command_file_dependency(c->u.subshell_command, l);
      check_single_file_dependency(c->input, l);
      check_single_file_dependency(c->output, l);
      break;
    }
}

/*lab 1c: parallel execution*/
int
execute_command_timetravel(command_t c)
{
  /* init file usage status if needed*/
  if (file_usage_stat_all == NULL)
    {
      file_usage_stat_all = make_file_usage_list();
    }
  //junk code to avoid warnings
  if (c == NULL)
    return -1;

  file_usage_list_t file_dependency = make_file_usage_list();
  check_command_file_dependency(c, file_dependency);
  
  pid_t pid;
  while ((pid = fork()) < 0);
  if (pid == 0)
    {
      file_usage_t f = file_dependency->head;
      while (f)
	{
	  if (f->pid != 0)
	    {
	      int status  = 0;
	      waitpid(pid, &status, 0);
	    }
	  f = f->next;
	}
      execute_command_standard(c);
    }
  else if (pid > 0)
    {
      file_usage_t f = file_dependency->head;
      while (f)
	{
	  if (1)
	    {
	      try_add_file_usage(file_usage_stat_all, f->file_name, pid);
	    }
	  f = f->next;
	}
    }
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
