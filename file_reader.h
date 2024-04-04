//
// Created by root on 11/9/23.
//

#ifndef PROJEKT_FAT_FILE_READER_H
#define PROJEKT_FAT_FILE_READER_H

#include <stdio.h>
#include <stdint.h>

//disk

struct disk_t {
    FILE *file;
    uint64_t sectors_count;
};

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);


//fat

struct volume_t {
    struct disk_t *disk;

    uint16_t bytes_per_sector; //bytes per sector (not used cuz we deals only with 512b per sectors)
    uint32_t total_sectors_count; //total sectors

    uint8_t sectors_per_cluster; //sectors per cluster
    uint32_t bytes_per_cluster; // cluster size in bytes

    uint16_t boot_sectors_count; //boot sectors count
    uint16_t fat_sectors_count; //all sectors count from fats

    uint32_t root_sectors_count; //root sectors count

    uint32_t data_sectors_count; //data sectors count
    uint32_t first_data_sector; //first data sector number

    uint16_t root_entries_count; //root entries


    uint16_t *fat; //fat table
    uint16_t fat_size; //how many entries in fat
};

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);

// file

struct cluster_chain_t {
    uint16_t *clusters;
    uint32_t size;
};

struct file_t {
    struct volume_t *volume;
    char *read_buf_base;
    char *read_buf_cur;
    char *read_buf_end;
    struct cluster_chain_t *chain;
    uint32_t offset;
    uint32_t size; //size of file
};

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);


// dir

struct date_t
{
    uint16_t day : 5;
    uint16_t month : 4;
    uint16_t year : 7;
};

struct time_t
{
    uint16_t seconds : 5;
    uint16_t minutes : 6;
    uint16_t hours : 5;
};

struct SFN {
    char filename[11];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    struct time_t creation_time;
    struct date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    struct time_t modified_time;
    struct date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;
} __attribute__((__packed__));

struct dir_t {
    struct volume_t *volume;
    uint16_t count;
    uint16_t index;
};

struct dir_entry_t {
    char name[13];
    uint32_t size;
    int8_t is_archived : 1;
    int8_t is_readonly : 1;
    int8_t is_system : 1;
    int8_t is_hidden : 1;
    int8_t is_directory;
};

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

#endif //PROJEKT_FAT_FILE_READER_H
