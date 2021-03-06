#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "path_lib.h"
#include "tar.h"
#include "utils.h"

//static char *end_of_path(char *path);
static void remove_last_slashs(char *path);
static enum file_type is_dir_type(int tar_fd, const char *filename);
static enum file_type is_reg_type(int tar_fd, const char *filename);

/* Return a malloc'd pointer of the last file in path, NULL if it end by . or ..
   the last file will also be detahced of path */
char *end_of_path(char *path)
{
  int len = strlen(path);
  char *end;
  if (path[len-1] == '/')
    {
      path[len-1] = '\0';
      end = strrchr(path, '/');
      path[len-1] = '/';
    }
  else
    {
      end = strrchr(path, '/');
    }
  if (end == NULL || end[1] == '\0') return NULL;
  if (end[1] == '.' &&
      (end[2] == '\0' || end[2] == '/' || (end[2] == '.' && (end[3] == '\0' || end[3] == '/'))))
    {
      return NULL;
    }
  char *res = malloc(strlen(end+1)+1);
  strcpy(res, end+1);
  end[1] = '\0';
  return res;
}

/* Remove any useless / at the end path (keep one if there is more than on / */
static void remove_last_slashs(char *path)
{
  int len = strlen(path);
  if (path[len-1] != '/') return;
  for (int i = 1; len-i > 0; i++)
    {
      if (path[len-i] == path[len-i-1])
	{
	  path[len-i] = '\0';
	}
      else return;
    }
}

char *split_tar_abs_path(char *path)
{
  if (path == NULL || path[0] != '/')
    {
      return NULL;
    }

  char *chr = path+1;

  while (*chr)
    {
      if(*chr == '/')
	{
	  *chr = '\0';
	  if (is_tar(path) == 1)
	    {
	      return chr + 1;
	    }
	  *chr = '/';
	}
      chr++;
    }

  if (is_tar(path) == 1)
    return chr;

  return NULL;
}


/* Reduce an absolute path (i.e. a path starting with a / ) */
char *reduce_abs_path(const char *path, char *resolved)
{
  if (path == NULL || path[0] != '/')
    {
      errno = EINVAL;
      return NULL;
    }

  if (path[0] == '\0')
    {
      errno = ENOENT;
      return NULL;
    }

  char *ret, *dest, *ret_end, *in_tar;

  if (resolved == NULL)
    {
      ret = malloc(PATH_MAX);
      if (ret == NULL)
	return NULL;
    }
  else
    ret = resolved;
  char path_cpy[PATH_MAX];
  strcpy(path_cpy, path);
  remove_last_slashs(path_cpy);
  char *end = end_of_path(path_cpy);
  ret[0] = '/';
  dest = ret + 1;
  ret_end = ret + PATH_MAX;
  in_tar = NULL;

  const char *name_start, *name_end;
  for (name_start = (name_end = path_cpy + 1); name_start[0] != '\0'; name_start = name_end)
    {
      // on saute les multiples /
      while (name_start[0] == '/')
	name_start++;

      // on cherche la fin du mot
      for (name_end = name_start; name_end[0] != '\0' && name_end[0] != '/'; name_end++)
	;


      if (name_end - name_start == 0) //on arrive à la fin de PATH
	break;
      else if (name_start[0] == '.' && name_end - name_start == 1) // ./
	;
      else if (name_start[0] == '.' && name_start[1] == '.' && name_end - name_start == 2) // ../
	{
	  if (dest > ret + 1) // si on n'est pas à la racine...
	    {
	      dest--;
	      while (dest[-1] != '/') // on fait revenir en arrière DEST
		dest--;
	    }
	}
      else
	{
	  int name_size = name_end - name_start;

	  if (name_end[0] == '/')
	    name_size++;

	  if (dest[-1] != '/')
	    {
	      dest[0] = '/';
	      dest++;
	    }

	  if (dest + name_size >= ret_end) // si on va dépasser la fin du buffer
	    {
	      if (resolved)
		{
		  errno = ENAMETOOLONG;
		  dest[0] = '\0'; // TODO: peut-être qu'on peut mieux corriger l'erreur
		  goto error;
		}
	      ptrdiff_t dest_offset = dest - ret;
	      size_t new_size = (ret_end - ret) + name_size;
	      ret = realloc (ret, new_size + 1);
	      dest = ret + dest_offset;
	    }

	  memmove (dest, name_start, name_size); // on copie le mot
	  dest += name_size; // on place dest à la fin du mot
	  dest[0] = '\0';

	  in_tar = split_tar_abs_path (ret);

	  struct stat st;
	  if (stat(ret, &st) < 0)
	    goto error;

	  if (in_tar == NULL && !S_ISDIR (st.st_mode) && name_end[0] != '\0') // pas de tar en jeu
	    {
	      errno = ENOTDIR;
	      goto error;
	    }
	  else if (in_tar != NULL)
	    {
	      if (in_tar[0] != '\0')
		{
		  if (tar_access(ret, in_tar, F_OK) < 0)
		    goto error;
		  in_tar[-1] = '/';
		}
	      else if (in_tar[-1] == '\0')
		in_tar[-1] = '/';
	    }
	}
    }

  /* if (dest > ret + 1 && dest[-1] == '/' && in_tar != NULL && in_tar[0] == '\0') */
  /*   --dest; */

  dest[0] = '\0';
  if (end)
    {
      strcat(ret, end);
      free(end);
    }
  return ret;

 error:
  if (resolved == NULL)
    free(ret);
  if (end)
    free(end);
  return NULL;
}

char *make_absolute (const char *path)
{
  char *abs;
  size_t path_len = strlen(path);

  if (*path == '/')
    {
      abs = copy_string(path);
    }
  else
    {
      char *pwd = getenv("PWD");
      abs = malloc(path_len + 2 + strlen(pwd));
      assert(abs);
      sprintf(abs, "%s/%s", pwd, path);
    }

  return abs;
}

int is_tar_path (char *path)
{
  if (path == NULL || *path != '/')
    return -1;


  char *chr = path+1;
  bool tar_path = false;

  while (*chr && !tar_path)
    {
      if(*chr == '/')
	{
	  *chr = '\0';

	  if (is_tar(path) == 1)
	    tar_path = true;

	  *chr = '/';
	}
      chr++;
    }

  if (is_tar(path) == 1)
    tar_path = true;

  return tar_path;
}

enum file_type ftype_of_file(int tar_fd, const char *filename, bool dir_priority)
{
  if (is_dir_name(filename))
    {
      if (ftar_access(tar_fd, filename, F_OK) > 0)
        return DIR;
      else
      {
        char filename_not_dir[PATH_MAX];
        strncpy(filename_not_dir, filename, strlen(filename) - 1);
        strcat(filename_not_dir, "");
        if (ftar_access(tar_fd, filename_not_dir, F_OK) > 0)
          errno = ENOTDIR;
        return NONE;
      }
    }
  enum file_type res;
  if (tar_fd < 0)
    return NONE;
  if (dir_priority)
    {
      res = is_dir_type(tar_fd, filename);
      if (res == DIR) return DIR;
      else if (res == NONE && errno == ENOENT)
	{
	  return is_reg_type(tar_fd, filename);
	}
      else return NONE;
    }
  else {
    res = is_reg_type(tar_fd, filename);
    if (res == REG) return REG;
    else if (res == NONE && errno == ENOENT)
      {
	return is_dir_type(tar_fd, filename);
      }
    else return NONE;
  }
}

enum file_type type_of_file(const char *tar_name, const char *filename, bool dir_priority)
{
  int tar_fd = open(tar_name, O_RDONLY);
  if (tar_fd < 0) return NONE;
  enum file_type res = ftype_of_file(tar_fd, filename, dir_priority);
  close(tar_fd);
  return res;
}

/* Return DIR if filename reference a directory, else NONE and errno is set accordingly */
static enum file_type is_dir_type(int tar_fd, const char *filename)
{
  char dir[PATH_MAX];
  sprintf(dir, "%s/", filename);
  return (ftar_access(tar_fd, dir, F_OK) > 0) ? DIR: NONE;
}

/* Return REG if filename reference a regular file, else NONE and errno is set accordingly */
static enum file_type is_reg_type(int tar_fd, const char *filename)
{
  return (ftar_access(tar_fd, filename, F_OK) == 1) ? REG : NONE;
}

int is_pwd_prefix(const char *tar_name, const char *filename)
{
  char *copy_tar_name = malloc(4096);
  strcpy(copy_tar_name, tar_name);

  char *copy_filename = malloc(100);
  strcpy(copy_filename, filename);

  char *path = malloc(4096);
  sprintf(path, "%s/%s", copy_tar_name, copy_filename);

  char *env = append_slash(getenv("PWD"));
  if(is_prefix(path, env) > 0)
    {
      free(copy_tar_name);
      free(copy_filename);
      free(path);
      free(env);
      return -1;
    }

  free(copy_tar_name);
  free(copy_filename);
  free(path);
  free(env);
  return 0;
}
