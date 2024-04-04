#include <stdio.h>
#include "file_reader.h"
#include <string.h>

int main() {
    struct disk_t* disk = disk_open_from_file("clothe_fat16_volume.img");
    struct volume_t* volume = fat_open(disk, 0);
    struct dir_t* dir = dir_open(volume, "\\");
    for (int i = 0; i < 11; ++i)
    {
        struct dir_entry_t entry;
        dir_read(dir, &entry);
        printf("%s\n", entry.name);
    }
    dir_close(dir);
    char filecontent[16140];
    struct file_t* file = file_open(volume, "CHARACTE.BIN");
    size_t size = file_read(filecontent, 1, 16140, file);
    file_seek(file, 0, SEEK_SET);
    printf("read: %zu bytes", size);
    file_close(file);
    fat_close(volume);
    disk_close(disk);
    return 0;
}
