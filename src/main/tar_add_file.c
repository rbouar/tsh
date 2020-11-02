#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "tar.h"
#include "utils.h"


static int seek_end_of_tar(int tar_fd) {
  struct posix_header hd;
  ssize_t read_size;
  while(1) {
    read_size = read(tar_fd, &hd, BLOCKSIZE);

    if(read_size != BLOCKSIZE)
      return -1;

    if (hd.name[0] != '\0')
      skip_file_content(tar_fd, &hd);
    else
      break;
  }

  lseek(tar_fd, -BLOCKSIZE, SEEK_CUR);
  return 0;
}

/* Set mtime of header to actual time */
static void set_hd_time(struct posix_header *hd) {
  time_t now;
  time(&now);
  sprintf(hd -> mtime, "%011o", (unsigned int) now);
}

static void init_type(struct posix_header *hd, struct stat *s) {
  if (S_ISREG(s -> st_mode)) {
    hd -> typeflag = REGTYPE;
  } else if (S_ISDIR(s -> st_mode)) {
    hd -> typeflag = DIRTYPE;
  } else if (S_ISCHR(s -> st_mode)) {
    hd -> typeflag = CHRTYPE;
  } else if (S_ISBLK(s -> st_mode)) {
    hd -> typeflag = BLKTYPE;
  } else if (S_ISLNK(s -> st_mode)) {
    hd -> typeflag = LNKTYPE;
  } else if (S_ISFIFO(s -> st_mode)) {
    hd -> typeflag = FIFOTYPE;
  } else {
    hd -> typeflag = AREGTYPE;
  }
}


static void init_mode(struct posix_header *hd, struct stat *s) {
  strcpy(hd -> mode, "0000000");
  int u_rights = (S_IRUSR & s -> st_mode) ? 4 : 0;
  int g_rights = (S_IRGRP & s -> st_mode) ? 4 : 0;
  int o_rights = (S_IROTH & s -> st_mode) ? 4 : 0;

  u_rights += (S_IWUSR & s -> st_mode) ? 2 : 0;
  g_rights += (S_IWGRP & s -> st_mode) ? 2 : 0;
  o_rights += (S_IWOTH & s -> st_mode) ? 2 : 0;

  u_rights += (S_IXUSR & s -> st_mode) ? 1 : 0;
  g_rights += (S_IXGRP & s -> st_mode) ? 1 : 0;
  o_rights += (S_IXOTH & s -> st_mode) ? 1 : 0;
  hd -> mode[5] = '0' + u_rights;
  hd -> mode[6] = '0' + g_rights;
  hd -> mode[7] = '0' + o_rights;
}

static int init_header(struct posix_header *hd, const char *filename) {
  struct stat s;
  if (stat(filename, &s) < 0) {
    return -1;
  }
  strcpy(hd -> name, filename);
  init_mode(hd, &s);
  sprintf(hd -> uid, "%07o", s.st_uid);
  sprintf(hd -> gid, "%07o" ,s.st_gid);
  sprintf(hd -> size, "%011lo", s.st_size);
  init_type(hd, &s);
  strcpy(hd -> magic, TMAGIC);
  set_hd_time(hd);
  hd -> version[0] = '0';
  hd -> version[1] = '0';
  // uname and gname are not added yet !
  set_checksum(hd);
  return 0;

}


/* Add two empty blocks at the end of a tar file */
static int add_empty_block(int tar_fd) {
  int size = 2 * BLOCKSIZE;
  char buf[size];
  memset(buf, '\0', size);
  if (write(tar_fd, buf, size) < 0) {
    return -1;
  }
  return 0;
}

/* Add file at path FILENAME to tar at path TAR_NAME */
int tar_add_file(const char *tar_name, const char *filename) {
  int src_fd = -1;
  if ((src_fd = open(filename, O_RDONLY)) < 0) {
    return error_pt(filename, &src_fd, 1);
  }
  int tar_fd = open(tar_name, O_RDWR);
  int fds[2] = {src_fd, tar_fd};
  if ( tar_fd < 0) {
    return error_pt(tar_name, fds, 2);
  }
  struct posix_header hd;
  memset(&hd, '\0', BLOCKSIZE);
  if (seek_end_of_tar(tar_fd) < 0) {
    return error_pt(tar_name, fds, 2);
  }
  if(init_header(&hd, filename) < 0) {
    return error_pt(filename, fds, 2);
  }
  if (write(tar_fd, &hd, BLOCKSIZE) < 0) {
    return error_pt(tar_name, fds, 2);
  }

  char buffer[BLOCKSIZE];
  ssize_t read_size;
  while((read_size = read(src_fd, buffer, BLOCKSIZE)) > 0) {
    if (read_size < BLOCKSIZE) {
      memset(buffer + read_size, '\0', BLOCKSIZE - read_size);
    }
    if (write(tar_fd, buffer, BLOCKSIZE) < 0) {
      return error_pt(tar_name, fds, 2);
    }
  }
  if (read_size < 0) {
    int fds[2] ={src_fd, tar_fd};
    return error_pt(filename, fds, 2);
  }
  add_empty_block(tar_fd);
  return 0;
}

int tar_append_file(const char *tar_name, const char *filename, int src_fd) {
  int tar_fd = open(tar_name, O_RDWR);
  if (tar_fd < 0) {
    return -1;
  }
  struct posix_header hd;
  if (seek_header(tar_fd, filename, &hd) != 1) {
    return error_pt(filename, &tar_fd, 1);
  }
  int size = get_file_size(&hd);
  off_t src_cur = lseek(src_fd, 0, SEEK_CUR);
  off_t src_size = lseek(src_fd, 0, SEEK_END) - src_cur;
  long unsigned int new_size = src_size + size;
  lseek(tar_fd, -BLOCKSIZE, SEEK_CUR);
  set_hd_time(&hd);
  sprintf(hd.size, "%011lo", new_size);
  set_checksum(&hd);
  write(tar_fd, &hd, BLOCKSIZE);
  int padding = BLOCKSIZE - (size % BLOCKSIZE);
  off_t beg = lseek(tar_fd, size, SEEK_CUR);
  off_t tar_size = lseek(tar_fd, 0, SEEK_END);

  if (fmemmove(tar_fd, beg + padding, tar_size - beg + padding, beg + src_size)) {
    close(tar_fd);
    return -1;
  }
  lseek(tar_fd, beg, SEEK_SET);
  lseek(src_fd, src_cur, SEEK_SET);
  if (read_write_buf_by_buf(src_fd, tar_fd, src_size, 512) != 0) {
    close(tar_fd);
    return -1;
  }
  // Add junk to finish block
  off_t cur = lseek(tar_fd, 0, SEEK_CUR);
  off_t move_size = lseek(tar_fd, 0, SEEK_END) - cur;
  int ret = fmemmove(tar_fd, cur, move_size, cur + BLOCKSIZE - (new_size % BLOCKSIZE));
  close(tar_fd);
  return ret;
}
