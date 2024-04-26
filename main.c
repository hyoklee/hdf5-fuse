/*
 * hdf5-fuse: fuse wrapper around the hdf5 file format
 */

#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fuse.h>
#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

hid_t root_group = -1;

size_t hdf5_fuse_filesize(const char* path)
{
  hid_t dataset = H5Dopen(root_group, path, H5P_DEFAULT);
  if(dataset < 0)
    return 0;

  hid_t datatype = H5Dget_type(dataset);
  hid_t dataspace = H5Dget_space(dataset);
  size_t type_size = H5Tget_size(datatype);
  size_t num_elems = H5Sget_simple_extent_npoints(dataspace);
  H5Sclose(dataspace);
  H5Dclose(dataset);
  return num_elems * type_size;
}

static int hdf5_fuse_getattr(const char* path, struct stat *stbuf)
{
  memset(stbuf, 0, sizeof(struct stat));

  H5O_info_t obj_info;
  if(H5Oget_info_by_name(root_group, path, &obj_info, H5P_DEFAULT, H5P_DEFAULT) < 0)
    return -ENOENT;

  if(obj_info.type == H5O_TYPE_GROUP) {
    stbuf->st_mode = S_IFDIR | 0555;
    H5G_info_t group_info;
    H5Gget_info_by_name(root_group, path, &group_info, H5P_DEFAULT);
    stbuf->st_nlink = 2 + group_info.nlinks;
    stbuf->st_size = group_info.nlinks;
  } else if (obj_info.type == H5O_TYPE_DATASET) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_size = hdf5_fuse_filesize(path);
  } else {
    stbuf->st_mode = S_IFCHR | 0000;
    stbuf->st_size = 0;
  }

  return 0;
}

static int hdf5_fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  H5G_info_t group_info;
  if(H5Gget_info_by_name(root_group, path, &group_info, H5P_DEFAULT) < 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  for(hsize_t i = 0; i < group_info.nlinks; ++i) {
    char name[128];
    H5Lget_name_by_idx(root_group, path,
        H5_INDEX_NAME, H5_ITER_INC, i, name, 128, H5P_DEFAULT);
    filler(buf, name, NULL, 0);
  }

  return 0;
}

static int hdf5_fuse_open(const char *path, struct fuse_file_info *fi)
{
  if((fi->flags & 3) != O_RDONLY)
    return -EACCES;

  H5O_info_t obj_info;
  if(H5Oget_info_by_name(root_group, path, &obj_info, H5P_DEFAULT, H5P_DEFAULT) < 0)
    return -ENOENT;

  return 0;
}

static int hdf5_fuse_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
  (void) fi;

  hid_t dataset = H5Dopen(root_group, path, H5P_DEFAULT);
  hid_t datatype = H5Dget_type(dataset);
  size_t buf_size = hdf5_fuse_filesize(path);
  char *hdf5_buf = malloc(buf_size);
  H5Dread(dataset, datatype, H5S_ALL, H5S_ALL, H5P_DEFAULT, hdf5_buf);
  size_t copy_size = buf_size - offset < size ? buf_size - offset : size;
  memcpy(buf, hdf5_buf+offset, copy_size);
  free(hdf5_buf);
  H5Dclose(dataset);
  return copy_size;
}

static struct fuse_operations hdf5_oper = {
  .getattr = hdf5_fuse_getattr,
  .readdir = hdf5_fuse_readdir,
  .open = hdf5_fuse_open,
  .read = hdf5_fuse_read,
};

int main(int argc, char** argv)
{
  if (argc != 3) {
    printf("usage: %s <mount point> <hdf5 file>\n", argv[0]);
    exit(0);
  }

  H5open();
  //Check for hdf5 file
  if (!H5Fis_hdf5(argv[2])) {
    printf("invalid hdf5 file: %s\n", argv[2]);
    exit(1);
  }

  //Attempt to open hdf5 file
  hid_t file = H5Fopen(argv[2], H5F_ACC_RDONLY, H5P_DEFAULT);
  if (file < 0) {
    printf("failed to open hdf5 file: %s\n", argv[2]);
    exit(1);
  }

  root_group = H5Gopen(file, "/", H5P_DEFAULT);

  int ret = fuse_main(argc - 1, argv, &hdf5_oper, NULL);
  H5Fclose(file);
  H5close();
  return ret;
}
