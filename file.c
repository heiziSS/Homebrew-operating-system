#include "bootpack.h"

// 将磁盘映像中的FAT解压缩
void file_readfat(int *fat, unsigned char *img)
{
    int i, j = 0;
    for (i = 0; i < 2880; i += 2) { // 磁盘中一共有2880个扇区
        fat[i] = (img[j] | (img[j + 1] << 8)) & 0xfff;
        fat[i + 1] = ((img[j + 1] >> 4) | (img[j + 2] << 4)) & 0xfff;
        j += 3;
    }
    return;
}

void file_loadfile(int clustno, int size, char *buf, int *fat, char *img)
{
    int i;
    int read_size;
    for (;;) {
        read_size = MIN(size, 512);
        for (i = 0; i < read_size; i++) {
            buf[i] = img[clustno * 512 + i];
        }
        size -= read_size;
        if (size > 0) {
            buf += read_size;
            clustno = fat[clustno];
        } else {
            break;
        }
    }
    return;
}
