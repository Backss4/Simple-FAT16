//
// Created by root on 11/9/23.
//

#include "file_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#define SIGNATURE                   (0xAA55)
#define SECTOR_SIZE                 (512)
#define MAX_SECTORS_PER_CLUSTER     (64)

#define IS_POWER_TWO(x)             (!((x) & ((x) - 1)) && (x))

#define FAT16_MIN_CLUSTERS          (4085)
#define FAT16_MAX_CLUSTERS          (65525)

#define ATTR_READ_ONLY              (1)
#define ATTR_HIDDEN                 (2)
#define ATTR_SYSTEM                 (4)
#define ATTR_VOLUME_ID              (8)
#define ATTR_DIRECTORY              (16)
#define ATTR_ARCHIVE                (32)

#define DIR_FREE                    (0xE5)
#define DIR_EOF                     (0x00)

#define NAME_LEN                    (8)
#define EXT_LEN                     (3)

//internal

const char ROOT_DIR[] = "\\";

struct boot_sector_t {
    char unused[3]; //Assembly code instructions to jump to boot code (mandatory in bootable partition)
    char name[8]; //OEM name in ASCII
    uint16_t bytes_per_sector; //Bytes per sector (512, 1024, 2048, or 4096)
    uint8_t sectors_per_cluster; //Sectors per cluster (Must be a power of 2 and cluster size must be <=32 KB)
    uint16_t reserved_sectors_count; //Size of reserved area, in sectors
    uint8_t number_of_fats; //Number of FATs (usually 2)
    uint16_t root_entries_count; //Maximum number of files in the root directory (FAT12/16; 0 for FAT32)
    uint16_t total_sectors_count; //Number of sectors in the file system; if 2 B is not large enough, set to 0 and use 4 B value in bytes 32-35 below
    uint8_t media_type; //Media type (0xf0=removable disk, 0xf8=fixed disk)
    uint16_t fat_size; //Size of each FAT, in sectors, for FAT12/16; 0 for FAT32
    uint16_t sectors_per_track; //Sectors per track in storage device
    uint16_t number_of_heads; //Number of heads in storage device
    uint32_t number_of_sectors_before_partition; //Number of sectors before the start partition
    uint32_t total_sectors_count32; //Number of sectors in the file system; this field will be 0 if the 2B field above(bytes 19 - 20) is non - zero

    uint8_t drive_number; //BIOS INT 13h(low level disk services) drive number
    uint8_t unused_1; //Not used
    uint8_t boot_signature; //Extended boot signature to validate next three fields (0x29)
    uint32_t serial_number; //Volume serial number
    char label[11];  //Volume label, in ASCII
    char type[8];  //File system type level, in ASCII. (Generally "FAT", "FAT12", or "FAT16")
    uint8_t unused_2[448]; //Not used
    uint16_t signature; //Signature value (0xaa55)
} __attribute__((__packed__));


int is_little_endian() {
    int x = 1;
    if (*(char *) &x == 1)
        return 0;
    return 1;
}

uint16_t to_little_endian(uint16_t x) {
    return (x << 8) | (x >> 8);
}

void full_file_name(const struct SFN *entry, char *buf) {
    int offset = 0;
    for (int i = 0; i < NAME_LEN; i++) {
        if (entry->filename[i] == ' ')
            break;
        *(buf + offset++) = entry->filename[i];
    }

    if (entry->filename[NAME_LEN] != ' ') {
        *(buf + offset++) = '.';

        for (int i = NAME_LEN; i < NAME_LEN + EXT_LEN; i++) {
            if (entry->filename[i] == ' ')
                break;
            *(buf + offset++) = entry->filename[i];
        }
    }
    *(buf + offset) = '\0';
}

struct cluster_chain_t *read_chain(const struct volume_t *pvolume, const struct SFN *dir_entry) {
    struct cluster_chain_t *chain = malloc(sizeof(struct cluster_chain_t));
    if (chain == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    chain->clusters = malloc(sizeof(uint16_t));
    if (chain->clusters == NULL) {
        free(chain);
        errno = ENOMEM;
        return NULL;
    }
    chain->size = 0;
    *(chain->clusters + chain->size++) = dir_entry->low_order_address_of_first_cluster;
    uint16_t next_cluster = *(pvolume->fat + dir_entry->low_order_address_of_first_cluster);
    while (next_cluster < 0xFFF8) {
        uint16_t *new_clusters = realloc(chain->clusters, (chain->size + 1) * sizeof(uint16_t));
        if (new_clusters == NULL) {
            free(chain->clusters);
            free(chain);
            errno = ENOMEM;
            return NULL;
        }

        chain->clusters = new_clusters;
        *(chain->clusters + chain->size++) = next_cluster;
        next_cluster = *(pvolume->fat + next_cluster);
    }

    return chain;
}

int strcasecmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *) s1;
    const unsigned char *p2 = (const unsigned char *) s2;

    int result = 0;
    if (p1 == p2)
        return 0;

    while ((result = tolower(*p1) - tolower(*p2++)) == 0)
        if (*p1++ == '\0')
            break;
    return result;
}

size_t file_read_internal(struct file_t *file, void *buf, size_t to_read) {
    //at this point we know we have to read data
    char *p = (char *) buf;

    //initialize cluster data to 0
    uint32_t current_cluster_idx = 0;
    uint16_t current_cluster = 0;
    uint32_t current_cluster_first_sector = 0;

    //get pointers to internal structures
    struct volume_t *volume = file->volume;
    struct cluster_chain_t *cluster_chain = file->chain;

    size_t available, remaining = to_read;

    //first check if we have something to read
    if (file->offset == file->size) {
        return 0; //nothing to read
    }
    //read bytes
    while (remaining > 0) {
        //available data count to read in buffer
        available = file->read_buf_end - file->read_buf_cur;

        //check if remaining bytes to read <= avail bytes
        if (remaining <= available) {
            //copy that data
            memcpy(p, file->read_buf_cur, remaining);
            file->read_buf_cur += remaining;
            file->offset += remaining;
            remaining = 0;
        } else {
            //check if we have avail data
            if (available > 0) {
                memcpy(p, file->read_buf_cur, available);
                file->read_buf_cur += available;
                file->offset += available;
                p += available;
                remaining -= available;
            }

            //check if we have any data to read
            if (file->offset == file->size)
                break;

            //at this point we can be pretty sure
            //that we are no at the end of file
            //and we don't have any data
            //update cluster metadata
            current_cluster_idx = file->offset / file->volume->bytes_per_cluster;
            current_cluster = cluster_chain->clusters[current_cluster_idx];
            current_cluster_first_sector = ((current_cluster - 2) * volume->sectors_per_cluster)
                                           + volume->first_data_sector;

            if (current_cluster_first_sector >= volume->total_sectors_count ||
                current_cluster_first_sector < volume->first_data_sector) {
                errno = ENXIO;
                return -1;
            }

            if (disk_read(volume->disk, current_cluster_first_sector, file->read_buf_base,
                          volume->sectors_per_cluster) == -1) {
                errno = ERANGE;
                return -1;
            }

            //update read pointers
            file->read_buf_cur = file->read_buf_base + file->offset % volume->bytes_per_cluster;
            if (current_cluster_idx == cluster_chain->size - 1) {
                file->read_buf_end = file->read_buf_base + (file->size - file->offset);
            } else {
                file->read_buf_end = file->read_buf_base + volume->bytes_per_cluster;
            }
        }
    }

    return to_read - remaining;
}

//api

struct disk_t *disk_open_from_file(const char *volume_file_name) {
    if (volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    FILE *fd = fopen(volume_file_name, "rb");
    if (fd == NULL) {
        if (errno != ENOMEM)
            errno = ENOENT;
        return NULL;
    }

    struct disk_t *disk = malloc(sizeof(struct disk_t));
    if (disk == NULL) {
        fclose(fd);
        errno = ENOMEM;
        return NULL;
    }
    fseek(fd, 0, SEEK_END);
    disk->file = fd;
    disk->sectors_count = ftell(fd) / SECTOR_SIZE;
    return disk;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {
    //check file and buffer
    if (pdisk == NULL || buffer == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (first_sector < 0 || sectors_to_read < 0 ||
        (uint32_t) first_sector + (uint32_t) sectors_to_read > pdisk->sectors_count) {
        errno = ERANGE;
        return -1;
    }

    if (fseek(pdisk->file, first_sector * SECTOR_SIZE, SEEK_SET) != 0) {
        return -1;
    }

    if (fread(buffer, SECTOR_SIZE, sectors_to_read, pdisk->file) != (uint32_t) sectors_to_read) {
        return -1;
    }

    return 0;
}

int disk_close(struct disk_t *pdisk) {
    if (pdisk == NULL || pdisk->file == NULL) {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->file);
    free(pdisk);
    return 0;
}

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {
    if (pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct volume_t *volume = malloc(sizeof(struct volume_t));
    if (volume == NULL) {
        errno = ENOMEM;
        return NULL;
    }


    struct boot_sector_t boot_sector;
    //read boot sector from disk
    if (disk_read(pdisk, first_sector, &boot_sector, 1) == -1) {
        free(volume);
        return NULL;
    }

    //check signature
    if (boot_sector.signature != SIGNATURE)
        goto err_ret;

    //check if sector is 512 BYTES long
    if (boot_sector.bytes_per_sector != SECTOR_SIZE)
        goto err_ret;
    volume->bytes_per_sector = SECTOR_SIZE;

    //check if sectors per cluster is good
    if (!IS_POWER_TWO(boot_sector.sectors_per_cluster) || boot_sector.sectors_per_cluster > MAX_SECTORS_PER_CLUSTER)
        goto err_ret;
    volume->sectors_per_cluster = boot_sector.sectors_per_cluster;
    volume->bytes_per_cluster = boot_sector.sectors_per_cluster * SECTOR_SIZE;

    //check if there is at least 1 fat
    if (boot_sector.number_of_fats == 0)
        goto err_ret;

    //check if size of fat != 0
    if (boot_sector.fat_size == 0)
        goto err_ret;

    volume->fat_sectors_count = boot_sector.number_of_fats * boot_sector.fat_size;
    //according to fatgen10.doc it's should be 1 for fat12/16
    //but they will support anything > 0
    if (boot_sector.reserved_sectors_count == 0)
        goto err_ret;
    volume->boot_sectors_count = boot_sector.reserved_sectors_count;

    /*if(boot_sector.root_entries_count == 0)
        goto err_ret;*/
    volume->root_entries_count = boot_sector.root_entries_count;
    volume->root_sectors_count =
            ((boot_sector.root_entries_count * sizeof(struct SFN)) + (boot_sector.bytes_per_sector - 1)) / SECTOR_SIZE;



    //check sectors number
    if (boot_sector.total_sectors_count == 0) {
        if (boot_sector.total_sectors_count32 == 0)
            goto err_ret;
        volume->total_sectors_count = boot_sector.total_sectors_count32;
    } else {
        volume->total_sectors_count = boot_sector.total_sectors_count;
    }

    uint32_t data_sectors_count = volume->total_sectors_count -
                                  (volume->boot_sectors_count + volume->fat_sectors_count + volume->root_sectors_count);


    /*data_sectors_count -= (boot_sector.reserved_sectors_count + (boot_sector.fat_size * boot_sector.number_of_fats));
    data_sectors_count -= ((boot_sector.root_entries_count * 32) / boot_sector.bytes_per_sector);*/

    uint32_t cluster_count = data_sectors_count / boot_sector.sectors_per_cluster;
    //check if is it FAT16, according to fatgen103.doc this is the way how it should be done
    //only this way works
    if (cluster_count < FAT16_MIN_CLUSTERS || cluster_count > FAT16_MAX_CLUSTERS)
        goto err_ret;

    volume->data_sectors_count = data_sectors_count;
    //fat size in bytes = fat sectors * SECTOR SIZE
    uint32_t fat_bytes = boot_sector.fat_size * SECTOR_SIZE;
    //alloc memory for fat tables
    //uint16_t cuz it only works for fat16
    uint16_t *fats = calloc(boot_sector.number_of_fats, fat_bytes);
    if (fats == NULL) {
        errno = ENOMEM;
        free(volume);
        return NULL;
    }

    //read all fat tables
    if (disk_read(pdisk, boot_sector.reserved_sectors_count, fats, boot_sector.fat_size * boot_sector.number_of_fats) ==
        -1) {
        free(fats);
        free(volume);
        return NULL;
    }


    //check fat tables
    for (uint8_t i = 0; i < boot_sector.number_of_fats - 1; i++) {
        if (memcmp(fats + i * fat_bytes, (uint8_t *) fats + (i + 1) * fat_bytes, fat_bytes) != 0) {
            free(fats);
            free(volume);
            errno = EINVAL;
            return NULL;
        }
    }

    volume->fat = malloc(fat_bytes);
    if (volume->fat == 0) {
        free(fats);
        free(volume);
        errno = ENOMEM;
        return NULL;
    }

    memcpy(volume->fat, fats, fat_bytes);
    if (is_little_endian()) {
        for (unsigned int i = 0; i < fat_bytes; i++) {
            *(volume->fat + i) = to_little_endian(*(volume->fat + i));
        }
    }


    volume->disk = pdisk;
    volume->fat_size = fat_bytes / sizeof(uint16_t);
    volume->first_data_sector = volume->boot_sectors_count + volume->fat_sectors_count + volume->root_sectors_count;
    free(fats);
    return volume;

    err_ret:
    free(volume);
    errno = EINVAL;
    return NULL;
}

int fat_close(struct volume_t *pvolume) {
    if (pvolume == NULL || pvolume->fat == NULL || pvolume->disk == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pvolume->fat);
    free(pvolume);
    return 0;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if (pvolume == NULL || pvolume->disk == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct file_t *file = malloc(sizeof(struct file_t));
    if (file == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    //create read buf of size SECTOR_SIZE * sectors_per_clster
    char *read_buf = malloc(sizeof(char) * pvolume->bytes_per_cluster);

    if (read_buf == NULL) {
        free(file);
        errno = ENOMEM;
        return NULL;
    }

    struct SFN *root_dir = calloc(pvolume->root_sectors_count, pvolume->bytes_per_sector);
    if (root_dir == NULL) {
        free(file);
        free(read_buf);
        errno = ENOMEM;
        return NULL;
    }

    if (disk_read(pvolume->disk, pvolume->boot_sectors_count + pvolume->fat_sectors_count, root_dir,
                  pvolume->root_sectors_count) != 0) {
        free(file);
        free(read_buf);
        free(root_dir);
        return NULL;
    }
    char temp_name[13];
    for (uint16_t i = 0; i < pvolume->root_entries_count; i++) {
        if (*((uint8_t *) (root_dir + i)->filename) == DIR_EOF)
            break;
        if (*((uint8_t *) (root_dir + i)->filename) == DIR_FREE)
            continue;

        full_file_name((root_dir + i), temp_name);
        if (strcasecmp(temp_name, file_name) == 0) {
            if ((root_dir + i)->file_attributes & ATTR_DIRECTORY || (root_dir + i)->file_attributes & ATTR_VOLUME_ID) {
                free(file);
                free(root_dir);
                free(read_buf);
                errno = EISDIR;
                return NULL;
            }
            file->chain = read_chain(pvolume, root_dir + i);
            if (file->chain == NULL) {
                if (errno != ENOMEM)
                    errno = EFAULT;
                free(file);
                free(root_dir);
                free(read_buf);
                return NULL;
            }
            file->size = (root_dir + i)->size;
            file->volume = pvolume;
            file->read_buf_base = read_buf;
            file->read_buf_end = file->read_buf_cur = read_buf + pvolume->bytes_per_cluster;
            file->offset = 0;
            free(root_dir);
            return file;
        }
    }

    free(file);
    free(root_dir);
    free(read_buf);
    errno = ENOENT;
    return NULL;
}

int file_close(struct file_t *stream) {
    if (stream == NULL || stream->chain == NULL) {
        errno = EFAULT;
        return 1;
    }
    free(stream->chain->clusters);
    free(stream->chain);
    free(stream->read_buf_base);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || stream == NULL || stream->chain == NULL || stream->volume == NULL) {
        errno = EFAULT;
        return -1;
    }

    size_t requested = nmemb * size;
    if (requested == 0)
        return 0;

    size_t read = file_read_internal(stream, ptr, requested);
    if (read == (size_t) -1) {
        return -1;
    }

    return read == requested ? nmemb : read / size;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {
    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    switch (whence) {
        case SEEK_SET: {
            if (offset < 0 || (uint32_t) offset > stream->size) {
                errno = ENXIO;
                return -1;
            }
            stream->offset = offset;
        }
            break;
        case SEEK_CUR: {
            if ((offset < 0 && (int64_t) stream->offset + offset < 0)
                || (uint64_t) stream->offset + offset > stream->size) {
                errno = ENXIO;
                return -1;
            }
            stream->offset += offset;
        }
            break;
        case SEEK_END: {
            if (offset > 0 || (int64_t) stream->size + offset < 0) {
                errno = ENXIO;
                return -1;
            }
            if (offset == 0) {
                stream->offset = stream->size;
            } else {
                stream->offset = stream->size + offset;
            }
        }
            break;
        default: {
            errno = EINVAL;
            return -1;
        }
    }

    stream->read_buf_cur = stream->read_buf_end; //invalidate read buffer
    return 0;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
    if (pvolume == NULL || dir_path == NULL) {
        errno = EFAULT;
        return NULL;
    }

    //only root dir
    if (strcmp(ROOT_DIR, dir_path) != 0) {
        errno = EFAULT;
        return NULL;
    }

    struct dir_t *dir = malloc(sizeof(struct dir_t));
    if (dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }


    dir->volume = pvolume;
    dir->count = pvolume->root_entries_count;
    dir->index = 0;

    return dir;
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (pdir->index > pdir->count) {
        errno = ENXIO;
        return -1;
    }

    struct SFN buf[SECTOR_SIZE / sizeof(struct SFN)];
    while (pdir->index <= pdir->count) {
        uint8_t entry_idx = pdir->index % 16;
        uint32_t sector_idx = pdir->index / 16;
        if (disk_read(pdir->volume->disk,
                      pdir->volume->boot_sectors_count + pdir->volume->fat_sectors_count + sector_idx, buf,
                      1) != 0) {
            errno = EIO;
            return -1;
        }
        if (*((uint8_t *) buf[entry_idx].filename) == DIR_EOF)
            break;
        if (*((uint8_t *) buf[entry_idx].filename) == DIR_FREE) {
            pdir->index += 1;
            continue;
        }

        full_file_name(&buf[entry_idx], pentry->name);
        pentry->size = buf[entry_idx].size;
        pentry->is_archived = buf[entry_idx].file_attributes & ATTR_ARCHIVE;
        pentry->is_readonly = buf[entry_idx].file_attributes & ATTR_READ_ONLY;
        pentry->is_system = buf[entry_idx].file_attributes & ATTR_SYSTEM;
        pentry->is_hidden = buf[entry_idx].file_attributes & ATTR_HIDDEN;
        pentry->is_directory = buf[entry_idx].file_attributes & ATTR_DIRECTORY;
        pdir->index += 1;
        return 0;
    }

    return 1;
}

int dir_close(struct dir_t *pdir) {
    if (pdir == NULL) {
        errno = EFAULT;
        return -1;
    }
    free(pdir);
    return 0;
}
