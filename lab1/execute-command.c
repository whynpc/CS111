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

/* the node of the link list used to record
   which command use which file */
struct file_usage {
  char *file_name;
  pid_t pid;
  struct file_usage *next;
};
typedef struct file_usage* file_usage_t;

/* define a linked-list, we also have the tail pointer for convenience */
struct file_usage_list {
  file_usage_t head;
  file_usage_t tail;
};
typedef struct file_usage_list* file_usage_list_t;

/* global records of the file usage by whole command stream */
file_usage_list_t file_usage_stat_all;

/* find whether a file has been used by a command in a file usage list */
static file_usage_t
retrieve_file_usage(file_usage_list_t l, char *file_name) 
{
  if (file_name == NULL)
    return NULL;

  file_usage_t p = l->head;
  while (p)
    {
      if (strcmp(p->file_name, file_name)==0)
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

/* you may want to use this function to add file usage; 
   selectively call update_file_usage() or add_file_usage() */
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

/* create a new file usage list */
static file_usage_list_t
make_file_usage_list()
{
  file_usage_list_t l;
  l = (file_usage_list_t) malloc(sizeof(struct file_usage_list));
  l->head = NULL;
  l->tail = NULL;
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
      if(infd==-1){printf("Fail to open %s\n",input);_exit(-1);}
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
      if(outfd==-1){printf("Fail to open %s\n",output);_exit(-1);}
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
    return 0;
  switch(c->type){
  case AND_COMMAND:{
    execute_command_standard(c->u.command[0]);
    if (command_status(c->u.command[0]) == 0)
      {
	execute_command_standard(c->u.command[1]);
	c->status = command_status(c->u.command[1]);
      }
    else
      {
	c->status = command_status(c->u.command[0]);
      }
    break;
  }
  case SEQUENCE_COMMAND:{
    execute_command_standard(c->u.command[0]);
    execute_command_standard(c->u.command[1]);
    c->status = command_status(c->u.command[1]);
    break;
  }
  case OR_COMMAND:{
    execute_command_standard(c->u.command[0]);
    if (command_status(c->u.command[0]) != 0)
      {
	execute_command_standard(c->u.command[1]);
	c->status = command_status(c->u.command[1]);
      }
    else
      {
	c->status = 0;
      }
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

    if(pid==0){	//child: execute a in a|b. Only write data
      close(pipefd[0]);	//close read end
      dup2(pipefd[1],STDOUT_FILENO);	//redirect stdout to pipe
      if(execute_command_standard(c->u.command[0])==-1)
	_exit(-1);
      _exit(command_status(c->u.command[0]));
    }
    else{	//parent: execute b in a|b. Only read data
      close(pipefd[1]);	//close write end
      dup2(pipefd[0],STDIN_FILENO);	//redirect stdin to pipe
      int status;
      waitpid(pid,&status,0);
      if(WEXITSTATUS(status)==-1)
	return -1;

      if(execute_command_standard(c->u.command[1])==-1)
	return -1;
      c->status = command_status(c->u.command[1]);
    }
    break;
  }
  case SIMPLE_COMMAND:{
    //create a new process
    pid_t pid;
    while ((pid = fork()) < 0);
	
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
	c->status = WEXITSTATUS(status);
      }
    break;
  }
  case SUBSHELL_COMMAND:{
    redirect_input(c->input);
    redirect_output(c->output);
    execute_command_standard(c->u.subshell_command);
    c->status = command_status(c->u.subshell_command);
    break;	
  }
  }

  return 0;	
}


/* check whehter a specified file has been used in the global
   file usage record; if so, take the prior command who
   uses this file as the dependency of current command;
   if not, still create a entry in the dependency to indicate
   the current command first use the file */
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
      add_file_usage(l, file_name, 0);
    }
}

/* check the command depends on which commands;
   save results in l, which is a local record only for this command */
static void
check_command_file_dependency(command_t c, file_usage_list_t l)
{
  if (c == NULL)
    return;

  switch (c->type)
    {
    case AND_COMMAND:
    case OR_COMMAND:
    case SEQUENCE_COMMAND:  // hopefully, there will be no sequence command here
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
  /* init global file usage records if needed */
  if (file_usage_stat_all == NULL)
    {
      file_usage_stat_all = make_file_usage_list();
    }

  // treat a sequence command as two separate command
  // there is a chance of paralism between the two subcommands
  if (c->type == SEQUENCE_COMMAND) 
    {
      execute_command_timetravel(c->u.command[0]);
      execute_command_timetravel(c->u.command[1]);
      /* at this stage, two subcommands already be sent
         to execute_command_standard(), we simply go for
         the next command */
      return 0;
    }

  //file_dependency lists all files that this command depends on, together with corresponding processes  
  file_usage_list_t file_dependency = make_file_usage_list();
  check_command_file_dependency(c, file_dependency);

  pid_t pid;
  while ((pid = fork()) < 0);	//wait until we can create a process
  if (pid == 0)
    {
      file_usage_t f = file_dependency->head;
      while (f)
	{
	  if (f->pid != 0)
	    {
	      //printf("Waiting for %d\n",f->pid);
	      //int status;
	      //waitpid(f->pid, &status, 0);	//child cannot wait for another child, so waitpid does not wait
	      while(kill(f->pid,0)!=-1);	//wait for a non-child process
	    }
	  f = f->next;	//goto next
	}
      printf("Executing ");
      print_command(c);
      execute_command_standard(c);
      _exit(command_status(c));
    }
  else if (pid > 0)
    {
      printf("pid=%d ", pid);
      print_command(c);
      file_usage_t f = file_dependency->head;
      while (f)
	{
	  try_add_file_usage(file_usage_stat_all, f->file_name, pid);
	  f = f->next;
	}
      //int status;
      //waitpid(pid,&status,0);
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
