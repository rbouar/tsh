#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "pipe.h"
#include "redirection.h"
#include "tokens.h"
#include "errors.h"
#include "list.h"
#include "array.h"

int exec_pipe(list *tokens)
{
  array *cmd_it;
  int size = list_size(tokens);
  int cpids[size];
  int pipes_fd[size-1][2];
  for (int i = 0; i < size; i++)
  {
    cmd_it = list_remove_first(tokens);
    if (i < size-1) pipe(pipes_fd[i]);
    switch((cpids[i] = fork()))
    {
      case -1:
        error_cmd("tsh", "fork");
        break;
      case 0: // Child
        free_tokens_list(tokens);
        if (i != 0)
        {
          add_reset_redir(STDIN_FILENO, 0);
          dup2(pipes_fd[i-1][0], STDIN_FILENO);
        }
        if (i != size -1)
        {
          close(pipes_fd[i][0]);
          add_reset_redir(STDOUT_FILENO, 0);
          dup2(pipes_fd[i][1], STDOUT_FILENO);
        }
        if (exec_red_array(cmd_it) != 0)
        {
          array_free(cmd_it, false);
          exit(EXIT_FAILURE);
        }
        remove_all_redir_tokens(cmd_it);
        if (array_size(cmd_it) <= 1)
        {
          reset_redirs();
          exit(EXIT_SUCCESS);
        }
        exec_cmd_array(cmd_it);
        break;
      default: // Parent
        if (i != 0) close(pipes_fd[i-1][0]);
        if (i != size -1) close(pipes_fd[i][1]);
        array_free(cmd_it, false);
        break;

    }
  }
  list_free(tokens, false); // List is empty
  for (int i = 0; i < size; i++)
  {
    waitpid(cpids[i], NULL, 0);
  }
  return 0;
}
