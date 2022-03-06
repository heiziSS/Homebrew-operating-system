#include "bootpack.h"

/*
    将磁盘映像中的FAT解压缩
    微软公司将FAT看作是最重要的磁盘信息，因此在磁盘中存放了2份FAT
    第1份FAT位于0x000200~0x0013ff
    第2份FAT位于0x001400~0x0025ff
*/
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

/*
    直接从压缩的fat镜像文件中获取对应的clustno
    clustno信息在镜像文件中存储方式如下：
    03 40 00 → 003 004
    ab cd ef   dab efc
*/
int get_clustno_from_fatimg(unsigned char *fatimg, int clustno)
{
    int m = (clustno >> 1) * 3; // 3个字节一组
    int n = clustno & 0x1;      // 该值表明是3个字节的前一部分或者后一部分
    if (n == 0) {
        return (fatimg[m] | (fatimg[m + 1] << 8)) & 0xfff;
    } else {
        return ((fatimg[m + 1] >> 4) | (fatimg[m + 2] << 4)) & 0xfff;
    }
}

void file_loadfile(FILEINFO *finfo, unsigned char *fatimg, char *img, char *buf)
{
    int i;
    int read_size;
    int clustno = finfo->clustno;
    int size = finfo->size;
    for (;;) {
        read_size = MIN(size, 512);
        for (i = 0; i < read_size; i++) {
            buf[i] = img[clustno * 512 + i];
        }
        size -= read_size;
        if (size > 0) {
            buf += read_size;
            clustno = get_clustno_from_fatimg(fatimg, clustno);
        } else {
            break;
        }
    }
    return;
}

FILEINFO *file_search(char *name)
{
    int i, j;
    char s[12];
    FILEINFO *finfo = (FILEINFO *) (ADR_DISKIMG + 0x002600);
	for (j = 0; j < 12; j++) {
		s[j] = ' ';
	}

    // 转换文件名为统一格式
    for (i = 0, j = 0; name[i] != 0; i++) {
        if (j >= 11)    return NULL; // 没有找到
        if (name[i] == '.' && j <= 8) {
            j = 8;
        } else {
            s[j] = name[i];
            if ('a' <= s[j] && s[j] <= 'z') {
                s[j] -= 0x20;   // 小写字母转换为大写字母
            }
            j++;
        }
    }

    // 搜索文件
    for (i = 0; i < FILE_MAX;) {
        if (finfo[i].name[0] == 0x00) {
            break;
        }
        if ((finfo[i].type & 0x18) == 0) {
            for (j = 0; j < 11; j++) {
                if (finfo[i].name[j] != s[j]) {
                    goto next_file;
                }
            }
            return finfo + i;    // 找到文件
        }
    next_file:
        i++;
    }
    return NULL;
}
