#include <xc.h>
#include <stddef.h>
#include "FSIO.h"
#include <spi.h>
#include <math.h>

// this cannot be greater than the number of sectors in a cluster
#define TOTAL_SECTORS 80L
#define BYTES_PER_SECTOR 512L

// Directory entry structure
typedef struct
{
    char      DIR_Name[DIR_NAMESIZE];           // File name
    char      DIR_Extension[DIR_EXTENSION];     // File extension
    BYTE      DIR_Attr;                         // File attributes
    BYTE      DIR_NTRes;                        // Reserved byte
    BYTE      DIR_CrtTimeTenth;                 // Create time (millisecond field)
    WORD      DIR_CrtTime;                      // Create time (second, minute, hour field)
    WORD      DIR_CrtDate;                      // Create date
    WORD      DIR_LstAccDate;                   // Last access date
    WORD      DIR_FstClusHI;                    // High word of the entry's first cluster number
    WORD      DIR_WrtTime;                      // Last update time
    WORD      DIR_WrtDate;                      // Last update date
    WORD      DIR_FstClusLO;                    // Low word of the entry's first cluster number
    DWORD     DIR_FileSize;                     // The 32-bit file size
}_DIRENTRY;

typedef _DIRENTRY * DIRENTRY;                   // A pointer to a directory entry structure
typedef FSFILE   * FILEOBJ;
int allocate_multiple_clusters(FSFILE*, DWORD);
BYTE FILEallocate_new_cluster(FSFILE *fo, BYTE mode);
DWORD FILEget_true_sector(FSFILE *);
DWORD ReadFAT (DISK *, DWORD);
DWORD Cluster2Sector(DISK *, DWORD);
DIRENTRY LoadDirAttrib(FILEOBJ fo, WORD *fHandle);
DWORD WriteFAT (DISK *dsk, DWORD ccls, DWORD value, BYTE forceWrite);
void IncrementTimeStamp(DIRENTRY dir);
BYTE Write_File_Entry( FILEOBJ fo, WORD * curEntry);
BYTE flushData (void);
extern BYTE gNeedFATWrite;
extern BYTE gNeedDataWrite;

/**
 * Connects to the SD card, resets FAT, attempts to write.
 *
 */
void NewSDWrite()
{
    DWORD CurrentSector = 0;
    DWORD SectorLimit = 0;
    DWORD clusterTracker[TOTAL_SECTORS] = {}; // debug
    DWORD sectorTracker[TOTAL_SECTORS] = {}; // debug
    while (!MDD_MediaDetect());
    while (!FSInit());
    char filename[] = "Three.txt";
    FSFILE * pointer = NULL;

    MDD_SDSPI_MediaInitialize(); // conect to sd card
    while (pointer == NULL) {
        pointer = FSfopen(filename, "w"); // open a file
    }

    unsigned char outbuf[BYTES_PER_SECTOR]; // generate some data
    unsigned long i;
    for (i = 0; i < BYTES_PER_SECTOR; i++) {
        outbuf[i] = i % 26 + 'a';
    }

    // Chain together the clusters we need
    DWORD num_clusters = ceilf(TOTAL_SECTORS / (float)pointer->dsk->SecPerClus);
    allocate_multiple_clusters(pointer, num_clusters);

    // Set the current cluster and sector to the first cluster of the file.
    //  #Problem: this will overwrite a file that's already written
    pointer->ccls = pointer->cluster;
    CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls);
    SectorLimit = CurrentSector + pointer->dsk->SecPerClus;

    // For each sector we want to write...
    for (i = 0; i < TOTAL_SECTORS; i++) {
        // Write the data
        int success = MDD_SDSPI_SectorWrite(CurrentSector, outbuf, 0);
        sectorTracker[i] = CurrentSector; // debug
        clusterTracker[i] = pointer->ccls; // debug
        if (!success) { // debug
            while (1);
        }

        // Check to see if we need to go to a new cluster;
        //  otherwise, next cluster
        if (CurrentSector == SectorLimit - 1) {
            // Set cluster and sector to next cluster in out chain
            pointer->ccls = ReadFAT(pointer->dsk, pointer->ccls);
            pointer->sec = 0;
            CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls);
            SectorLimit = CurrentSector + pointer->dsk->SecPerClus;
        } else {
            CurrentSector++;
        }
    }
    gNeedFATWrite = TRUE;
    gNeedDataWrite = FALSE;
    // save off the positon
    pointer->pos = BYTES_PER_SECTOR-1; // current position in sector (bytes)

    // save off the seek
    pointer->seek = TOTAL_SECTORS * BYTES_PER_SECTOR; // current position in file (bytes)

    // now the new size
    //  #Problem: This might not be accurate
    pointer->size = TOTAL_SECTORS * BYTES_PER_SECTOR; // size of file (bytes)
    FSfclose(pointer);
}

/**
 * Allocate more clusters for a file.
 * @param fo File Object of the file you want to extend.
 * @param num_clusters The number of clusters to allocate.
 * @return Returns the number of successful allocations.
 */
int allocate_multiple_clusters(FSFILE* fo, DWORD num_clusters)
{
    int i;
    for (i = 0; i < num_clusters; i++) {
        if (FILEallocate_new_cluster(fo, 0) != CE_GOOD) {
            return i;
        }
    }
    return num_clusters;
}

/**
 * Correct the sector and cluster data for a file to be valid numbers.
 * Not tested or used.
 * @param fo The file to normalize
 */
void normalize_file_data(FSFILE* fo)
{
    fo->ccls += fo->sec / fo->dsk->SecPerClus;  // The current cluster of the file
    fo->sec = fo->sec%fo->dsk->SecPerClus;      // The current sector in the current cluster of the file
}

/**
 * Write a sector's worth of data to the sd card. First call creates a new file.
 * Successive calls write to the same file.
 * @param outbuf Pointer to the data to write
 * @return 1 if successful, 0 othersise.
 */
int NewSDSimpleWriteSector(const unsigned char outbuf[BYTES_PER_SECTOR])
{
    DWORD CurrentSector = 0;
    DWORD SectorLimit = 0;
    static DWORD currentCluster;
    static int firstCall = 1;
    while (!MDD_MediaDetect()); // !! make this smarter
    const char filename[] = "simple.txt";
    FSFILE * pointer = NULL;

    // connect to sd card (!! similar to MDD_MediaDetect)
    MDD_SDSPI_MediaInitialize();

    // Open a new file the first time, append it otherwise.
    if (firstCall){
        while (pointer == NULL) pointer = FSfopen(filename, "w");
    } else {
        while (pointer == NULL) pointer = FSfopen(filename, "a");
    }

    // Set the current cluster and sector to the first cluster of the file.
    //  #Problem: this will overwrite a file that's already written
    if (firstCall) {
        pointer->ccls = pointer->cluster;
    } else {
        pointer->ccls = currentCluster;
    }

    // Calculate the real sector number of our current place in the file.
    CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls) + pointer->sec;
    SectorLimit = Cluster2Sector(pointer->dsk, pointer->ccls) + pointer->dsk->SecPerClus;
    
    // Write the data
    int success = MDD_SDSPI_SectorWrite(CurrentSector, outbuf, 0);
    if (!success) { // debug
        return 0;
    }

    // Check to see if we need to go to a new cluster;
    //  otherwise, next cluster
    if (CurrentSector == SectorLimit - 1) {
        // Set cluster and sector to next cluster in out chain
        pointer->ccls = ReadFAT(pointer->dsk, pointer->ccls);
        pointer->sec = 0;
        CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls);
        SectorLimit = CurrentSector + pointer->dsk->SecPerClus;
    } else {
        pointer->sec++;
    }
    
    gNeedFATWrite = TRUE;
    gNeedDataWrite = FALSE;
    // save off the positon
    pointer->pos = BYTES_PER_SECTOR-1; // current position in sector (bytes)

    // save off the seek
    pointer->seek += BYTES_PER_SECTOR; // current position in file (bytes)

    // now the new size
    //  #Problem: This might not be accurate
    pointer->size += BYTES_PER_SECTOR; // size of file (bytes)
    currentCluster = pointer->ccls;
    FSfclose(pointer);
    firstCall = 0;
    return 1;
}

/**
 * Initialize for writing to the SD card using NewSDSimpleWriteSector
 */
void NewSDSimpleInit()
{
    while (!MDD_MediaDetect()); // !! make this smarter
    while (!FSInit());
}

/**
 * Initalizes filesystems and returns a file structure for use with
 * NewSDWriteSector.
 * @param filename A string with the filename (name.ext)
 * @return A file structure
 */
FSFILE * NewSDInit(char *filename)
{
    FSFILE * pointer = NULL;

    while (!MDD_MediaDetect()); // !! make this smarter
    while (!FSInit());

    // Open a new file
    while (pointer == NULL) pointer = FSfopen(filename, "w");

    // Initialize data for NewSDWriteSector
    pointer->ccls = pointer->cluster;

    gNeedFATWrite = TRUE;
    gNeedDataWrite = FALSE;
    NewFileUpdate(pointer);
    return pointer;
}

/**
 * Does not work. Proposed functionality:
 * Writes the data in outbuf to the file (pointer)
 * @param pointer The FSFILE to write to
 * @param outbuf An array of data (BYTES_PER_SECTOR in legnth)
 * @return 1 if successful, 0 otherwise. 
 */
int NewSDWriteSector(FSFILE *pointer, unsigned char outbuf[BYTES_PER_SECTOR])
{
    // Calculate the real sector number of our current place in the file.
    DWORD CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls)
            + pointer->sec;
    DWORD SectorLimit = Cluster2Sector(pointer->dsk, pointer->ccls)
            + pointer->dsk->SecPerClus;

    // Write the data
    int success = MDD_SDSPI_SectorWrite(CurrentSector, outbuf, 0);
    if (!success) { // debug
        return 0;
    }

    // Check to see if we need to go to a new cluster;
    //  otherwise, next cluster
    if (CurrentSector == SectorLimit - 1) {
        // Set cluster and sector to next cluster in out chain
        pointer->ccls = ReadFAT(pointer->dsk, pointer->ccls);
        pointer->sec = 0;
        CurrentSector = Cluster2Sector(pointer->dsk, pointer->ccls);
        SectorLimit = CurrentSector + pointer->dsk->SecPerClus;
    } else {
        pointer->sec++;
    }

    gNeedFATWrite = TRUE;
    gNeedDataWrite = FALSE;

    // save off the positon
    pointer->pos = BYTES_PER_SECTOR-1; // current position in sector (bytes)

    // save off the seek
    pointer->seek += BYTES_PER_SECTOR; // current position in file (bytes)

    // now the new size
    //  #Problem: This might not be accurate
    pointer->size += BYTES_PER_SECTOR; // size of file (bytes)
    NewFileUpdate(pointer);

    return 1;
}

/**
 * Mimics FSfileClose to write file info to the SD card.
 * @param fo The file to update
 */
void NewFileUpdate(FSFILE * fo)
{
    DIRENTRY dir;
    WORD fHandle = fo->entry;

    // Write the file data
//    if(flushData()) while(1);
    WriteFAT (fo->dsk, 0, 0, TRUE);
    

    // Isn't correct anyways
    //IncrementTimeStamp(dir);

    // Update file entry data
    dir = LoadDirAttrib(fo, &fHandle);
    dir->DIR_FileSize = fo->size;
    dir->DIR_Attr = fo->attributes;
    dir->DIR_FstClusHI= (fo->cluster&0xFFFF0000)>>16;
    dir->DIR_FstClusLO= fo->cluster&0x0000FFFF;
    Write_File_Entry(fo,&fHandle);
}