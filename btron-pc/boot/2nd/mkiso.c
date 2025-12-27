/*
 * Simple ISO 9660 creator for B-Free OS
 * Creates a minimal bootable ISO from a boot image
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define SECTOR_SIZE 2048
#define SYSTEM_AREA_SIZE (16 * SECTOR_SIZE)

typedef struct {
    unsigned char type_code;
    unsigned char d1[8];
    unsigned char platform_id[4];
    unsigned char d2[2];
    unsigned char language_code[2];
    unsigned char system_boot_code[32];
    unsigned char padding[40];
} validation_entry_t;

typedef struct {
    unsigned char boot_indicator;
    unsigned char platform_id;
    unsigned char boot_medium_type;
    unsigned char load_segment;
    unsigned char system_type;
    unsigned char padding[3];
    unsigned short load_count;
    unsigned int load_lba;
    unsigned char padding2[20];
} boot_entry_t;

int main(int argc, char *argv[]) {
    FILE *infile, *outfile;
    unsigned char sector_buffer[SECTOR_SIZE];
    unsigned char boot_data[65536];
    size_t boot_size;
    struct stat st;
    int boot_lba;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <boot_image> <output.iso>\n", argv[0]);
        return 1;
    }
    
    /* Open boot image */
    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror("Cannot open boot image");
        return 1;
    }
    
    /* Read boot image size */
    fstat(fileno(infile), &st);
    boot_size = st.st_size;
    
    if (boot_size > sizeof(boot_data)) {
        fprintf(stderr, "Boot image too large\n");
        fclose(infile);
        return 1;
    }
    
    /* Read boot image */
    if (fread(boot_data, 1, boot_size, infile) != boot_size) {
        perror("Cannot read boot image");
        fclose(infile);
        return 1;
    }
    fclose(infile);
    
    /* Open output ISO */
    outfile = fopen(argv[2], "wb");
    if (!outfile) {
        perror("Cannot create ISO file");
        return 1;
    }
    
    /* Write system area (zeroed) */
    memset(sector_buffer, 0, SECTOR_SIZE);
    for (int i = 0; i < 16; i++) {
        if (fwrite(sector_buffer, SECTOR_SIZE, 1, outfile) != 1) {
            perror("Write error");
            fclose(outfile);
            return 1;
        }
    }
    
    /* Write primary volume descriptor */
    memset(sector_buffer, 0, SECTOR_SIZE);
    sector_buffer[0] = 1;  /* Primary Volume Descriptor */
    memcpy(&sector_buffer[1], "CD001", 5);  /* Standard identifier */
    sector_buffer[6] = 1;  /* Version */
    
    /* Volume set size (1 volume) */
    sector_buffer[80] = 1;
    sector_buffer[81] = 0;
    sector_buffer[82] = 0;
    sector_buffer[83] = 1;
    
    if (fwrite(sector_buffer, SECTOR_SIZE, 1, outfile) != 1) {
        perror("Write error");
        fclose(outfile);
        return 1;
    }
    
    /* Write terminator */
    memset(sector_buffer, 0, SECTOR_SIZE);
    sector_buffer[0] = 255;  /* Terminator */
    memcpy(&sector_buffer[1], "CD001", 5);
    sector_buffer[6] = 1;
    
    if (fwrite(sector_buffer, SECTOR_SIZE, 1, outfile) != 1) {
        perror("Write error");
        fclose(outfile);
        return 1;
    }
    
    /* Write boot image (padded to sector boundary) */
    boot_lba = ftell(outfile) / SECTOR_SIZE;
    
    if (fwrite(boot_data, 1, boot_size, outfile) != boot_size) {
        perror("Write error");
        fclose(outfile);
        return 1;
    }
    
    /* Pad to sector boundary */
    int padding = SECTOR_SIZE - (boot_size % SECTOR_SIZE);
    if (padding != SECTOR_SIZE) {
        memset(sector_buffer, 0, padding);
        if (fwrite(sector_buffer, 1, padding, outfile) != padding) {
            perror("Write error");
            fclose(outfile);
            return 1;
        }
    }
    
    fclose(outfile);
    
    printf("ISO created successfully: %s\n", argv[2]);
    printf("Boot image size: %zu bytes\n", boot_size);
    printf("Boot LBA: %d\n", boot_lba);
    
    return 0;
}
