#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/limits.h>
#include <errno.h>

#include "command_handler.h"
#include "errors.h"
#include "tar.h"
#include "path_lib.h"
#include "utils.h"

#define CMD_NAME "cat"

int cat (char *tar_name, char *filename, char *options)
{
  if (is_empty_string(filename))
  {
    errno = EISDIR;
    error(EISDIR, "%s: %s", CMD_NAME, tar_name);
    return EXIT_FAILURE;
  }
  enum file_type t = type_of_file(tar_name, filename, false);
  switch(t)
  {
    case REG:
      break;
    case DIR:
      errno = EISDIR;
    case NONE:
      tar_error_cmd(CMD_NAME, tar_name, filename);
      return EXIT_FAILURE;
  }
  if (tar_cp_file(tar_name, filename, STDOUT_FILENO) != 0)
    {
      tar_error_cmd (CMD_NAME, tar_name, filename);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

int main (int argc, char *argv[])
{
  unary_command cmd =
    {
      CMD_NAME,
      cat,
      false,
      false,
      ""
    };

  return handle_unary_command (cmd, argc, argv);
}
