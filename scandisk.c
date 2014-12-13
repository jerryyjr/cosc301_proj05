#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

int get_clust_size(uint16_t Cluster, uint8_t *image_buf, struct bpb33* bpb){
	int num=0;
	while(!is_end_of_file(Cluster)){
		num++;
		Cluster=get_fat_entry(Cluster,image_buf,bpb);
	}
	return num;
}

void update_ref(char* clust_ref,struct direntry *dirent, uint8_t *image_buf, struct bpb33* bpb){
	uint16_t Cluster=getushort(dirent->deStartCluster);
	while(!is_end_of_file(Cluster)){
		if(clust_ref[Cluster]!='9'){
			clust_ref[Cluster]='0';
		}
		Cluster=get_fat_entry(Cluster,image_buf,bpb);
	}
}

void fix_clust(uint16_t Cluster, uint8_t *image_buf, struct bpb33* bpb,uint32_t size){
	uint16_t precluster=0;
	uint16_t end=0;
	while(is_valid_cluster(Cluster,bpb)&&(int)size!=0){
		precluster=Cluster;
		Cluster=get_fat_entry(Cluster,image_buf,bpb);
		size--;
	}
	end=precluster;
	while(is_valid_cluster(precluster,bpb)){
		uint16_t pcluster=get_fat_entry(precluster,image_buf,bpb);
		set_fat_entry(precluster, FAT12_MASK&CLUST_FREE, image_buf, bpb);
		precluster=pcluster;
	}
	set_fat_entry(end,FAT12_MASK&CLUST_EOFS,image_buf,bpb);
}

void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}


uint16_t print_dirent(struct direntry *dirent, int indent, uint8_t *image_buf, struct bpb33* bpb,char* clust_ref)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size1;
    uint32_t size2;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
        update_ref(clust_ref,dirent,image_buf,bpb);
	    print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
    update_ref(clust_ref,dirent,image_buf,bpb);
	size1 = getulong(dirent->deFileSize);
	size2 = getulong(dirent->deFileSize)/512+1;
	int clustsize=get_clust_size(getushort(dirent->deStartCluster),image_buf,bpb);
	if ((int)size2!=clustsize){
		print_indent(indent);
		printf("s%d c%d ",size2,clustsize);
		printf("%s.%s (%u bytes) (starting cluster %d) inconsistent \n", 
	       name, extension, size1, getushort(dirent->deStartCluster)) ;
	    if (size2>clustsize){
	    	
	    	putulong(dirent->deFileSize,clustsize*511);
	    }else{
	    	
	    	fix_clust(getushort(dirent->deStartCluster),image_buf,bpb,size2);
	    }
	   // printf("inconsistency fixed");
	}
	
	/*print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');*/
    }

    return followclust;
}


void follow_dir(uint16_t cluster, int indent,
		uint8_t *image_buf, struct bpb33* bpb,char* clust_ref){
    while (is_valid_cluster(cluster, bpb))
    {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
            
            uint16_t followclust = print_dirent(dirent, indent, image_buf,bpb,clust_ref);
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb, clust_ref);
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}


void traverse_root(uint8_t *image_buf, struct bpb33* bpb,char* clust_ref)
{
    uint16_t cluster = 0;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0,image_buf,bpb,clust_ref);
        if (is_valid_cluster(followclust, bpb))
            follow_dir(followclust, 1, image_buf, bpb,clust_ref);

        dirent++;
    }
}

char* check_clust(uint8_t *image_buf, struct bpb33* bpb){
	int total_cluster=bpb->bpbSectors / bpb->bpbSecPerClust;
	char* cluster_ref=malloc(sizeof(char)*total_cluster);
	memset(cluster_ref,'0',sizeof(char)*total_cluster);
	int i;
	for (i = 2; i < total_cluster; i++) 
	    {
	    	uint16_t flcluster = get_fat_entry(i, image_buf, bpb);
			if (flcluster!= CLUST_FREE)
			{
				if(flcluster==(FAT12_MASK & CLUST_BAD)){
					cluster_ref[i]='9';
				}else{
			    	cluster_ref[i]='1';
			    }
			}
	    }
	    
	return cluster_ref;
}

void check_update(char* clust_ref){
	for(int i=2;i<strlen(clust_ref);i++){
		if(clust_ref[i]=='1'){
			printf("found orphan%d\n",i);
		}
		if(clust_ref[i]=='9'){
			printf("found bad%d\n",i);
		}
	}
}

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
	usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
	char* clust_ref=check_clust(image_buf,bpb);
	traverse_root(image_buf, bpb, clust_ref);
    // your code should start here...
	check_update(clust_ref);
	




    unmmap_file(image_buf, &fd);
    return 0;
}
