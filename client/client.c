/* APP: DartSync	MODULE: client 
 * client.c 	- standalone application to open, monitor, and manage a 
 *				file system that is shared between other peers.  Need to be 
 *				able to initialize a shared folder with a tracker, share it
 *				with peers, receive changes from peers, push changes to peers,
 *				and drop in and out of the network of tracker and peers.
 */

/* ------------------------- System Libraries -------------------------- */
#include <stdio.h>		//print
#include <stdlib.h>		//exit
#include <unistd.h>		//mkdir
#include <sys/stat.h>	//mkdir
#include <arpa/inet.h>	// in_addr_t
#include <pthread.h>	//thread stuff
#include <signal.h>
#include <wordexp.h> // for shell expansion of ~
#include <string.h> // strdup
#include <limits.h>
#include <sys/time.h>
	 
/* -------------------------- Local Libraries -------------------------- */
#include "client.h"
#include "network_client.h"
#include "../utility/FileSystem/FileSystem.h"
#include "../utility/ChunkyFile/ChunkyFile.h"
#include "../utility/FileTable/FileTable.h"
#include "../common/constant.h"

/* ----------------------------- Constants ----------------------------- */
#define CONN_ACTIVE (0)
#define CONN_CLOSED (1)
//#define GET_ALL_CHUNKS 99999

// for filetable_add_filesystem pending request logic
#define NEED_DATA (1)
#define HAVE_DATA (0)

#define MAX_PENDING_REQUESTS (15)

/* ------------------------- Global Variables -------------------------- */
CNT* cnt;
FileSystem *cur_fs;
FileSystem *prev_fs = NULL;
struct timeval prev_time;

FileTable *ft;
char * dartsync_dir; 	// global of absolute dartsync_dir path
int myID = 0;
int sleep_time;

/* ------------------------------- TODO -------------------------------- */

/* add password recognition */

/* ----------------------------- Questions ----------------------------- */

// should chunk be already malloc'd?  what about chunk size?
// how to destroy a chunky file and a chunk?

/* ---------------------- Private Function Headers --------------------- */

/* check if file system is empty, used to check diffs for anything */
int CheckFileSystem(FileSystem *fs);

/* 
 * iterate over a filesystem that is assumed to be the deletions filesystem
 * after a diff and delete any files that it points to
 */
int RemoveFileDeletions(FileSystem *deletions);

/* 
 * iterate over a filesystem that is assumed to be the additions filesystem
 * after a diff and make requests for the files' chunks from peers
 */
int GetFileAdditions(FileSystem *additions, int author_id);

// returns the user's argument for which directory they'd like to sychronize
//	root_arg : (not claimed) root argument from user
//	ret : (not claimed) allocated, expanded, checked user argument for root (must be claimed)
char * get_absolute_root(char * root_arg) {
	if (!root_arg) {
		fprintf(stderr,"get_absolute_root error: null argument\n");
		return NULL;
	}

	if (strlen(root_arg) < 1) {
		fprintf(stderr,"get_absolute_root error: root_arg is zero chars long\n");
		return NULL;
	}

	/* make sure sync directory isn't root */
	if (strlen(root_arg) == 1 && strcmp(root_arg, "/") == 0) {
		fprintf(stderr,"DARTSYNC ERROR! DON'T SYNCHRONIZE ROOT!\n");
	 	return NULL;
	}

	/* make sure sync directory argument doesn't end in "/" */
	if (root_arg[strlen(root_arg)-1] == '/') {
		root_arg[strlen(root_arg)] = '\0';
	}

	/* check if the folder already exists, if it doesn't then make it */
	char * new_dir = tilde_expand(root_arg);;
	if (0 != access(new_dir, (F_OK)) ){
		printf("Cannot access %s -- creating directory\n", new_dir);
		perror("reason");

		/* it doesn't exist, so make it */
		if (-1 == mkdir(new_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
			printf("CLIENT MAIN: failed to create sync dir (%s)\n", new_dir);
			return NULL;
		}

		root_arg = new_dir;
	} 

	/* check to make sure argument is a directory */
	struct stat statbuf;
	if (stat(new_dir, &statbuf) == -1) {
		fprintf(stderr,"get_absolute_root error: stat error on %s\n", root_arg);
		return NULL;
	}

	/* make sure it is directory? */
	if (!S_ISDIR(statbuf.st_mode)) {
		fprintf(stderr,"get_absolute_root error: %s is not a directory\n", root_arg);
		return NULL;
	}

	/* make sure you get the absolute path */
	char * absolute_path = realpath(new_dir, NULL);
	if (!absolute_path){
		fprintf(stderr,"get_absolute_root error: failed to get real path of %s\n", root_arg);
		return NULL;
	}
	
	if (new_dir != NULL) {
		free(new_dir);
	}

	printf("CLIENT -- using DartSync Root as %s\n", absolute_path);
	return absolute_path;
}

// appends relative_path onto root
//	relative_path : (not claimed) relative path
// 	root : (not claimed) Dart sync root path
//	ret : (not claimed) new concatenated string (caller must claim)
char * append_DSRoot(char * relative_path, char * root) {
	if (!root || !relative_path) {
		fprintf(stderr,"append_DSroot error: null arguments\n");
		return NULL;
	}

	if (root[strlen(root)-1] == '/') {
		fprintf(stderr,"append_DSroot error: root directory string shouldn't end in a slash!\n");
		return NULL;
	}

	//printf("catting strings %s and %s\n", root, relative_path);
	char * result = (char *)malloc(strlen(relative_path) + strlen(root) + 1);
	memcpy(result, root, strlen(root) + 1);
	strcat(result, relative_path);
	//printf("result: %s\n", result);

	return result;
}

// expands the ~/dart_sync dir
//	original_path : (not claimed) DARTSYNC_DIR
//	ret : (not claimed) expanded path (allocated string on heap)
char * tilde_expand(char * original_path) {
	if (!original_path)
		return NULL;

	printf("expanding string %s\n", original_path);

	char * expanded_string = NULL;
	wordexp_t exp_result;
	wordexp(original_path, &exp_result, 0);
	expanded_string = strdup(exp_result.we_wordv[0]);
	wordfree(&exp_result);

	printf("expanded_string %s\n", expanded_string);
  	return expanded_string;
}

// takes an absolute file path and replaces the path to dart_sync with a tilda
//	original_path: (not claimed) the file path to compress
// 	ret: (not claimed) the compressed filepath
char * tilde_compress(char * original_path){
	if (!original_path){
		return NULL;
	}
	// printf("Tilde compressing string %s\n", original_path);
	
	for (int i = strlen(original_path)-1; i >= 9; i--){
		if (original_path[i] == 'c' &&
			original_path[i-1] == 'n' &&
			original_path[i-2] == 'y' &&
			original_path[i-3] == 's' &&
			original_path[i-4] == '_' &&
			original_path[i-5] == 't' &&
			original_path[i-6] == 'r' &&
			original_path[i-7] == 'a' &&
			original_path[i-8] == 'd'
			)
		{
			char buffer[strlen(original_path)-i+10]; // + /dart_sync\0
			memset(buffer, 0 , sizeof(buffer));
			// printf("moving: %s\n", &original_path[i+1]);
			memcpy(buffer, &original_path[i-9], strlen(original_path)-(i-9)*sizeof(char));
			buffer[strlen(original_path)-i+9+1] = '\0';
			// printf("set new original address\n");
			// original_path += (i+1)*sizeof(char);
			// printf("New original path: %s\n", buffer);
			char * compressed_string = (char*)malloc(sizeof(buffer)+sizeof(char));
			sprintf(compressed_string, "~%s", buffer);
			// printf("compressed_string: %s\n", compressed_string);
			return compressed_string;
		}
	}
	return NULL;
}

/* ----------------------- Public Function Bodies ---------------------- */

int SendMasterFSRequest(FileSystem *cur_fs){
	FileSystem *master;

	// MATT FLAG -- ADDITIVE START UP LOGIC ?
	// if (-1 == send_status(cnt, cur_fs)){
	// 	printf("SendMasterFSRequest: send_status failed\n");
	// 	return -1;
	// }

	printf("Send master FS request\n");
	if (-1 == send_request_for_master(cnt)){
		printf("SendMasterFSRequest: send_request_for_master() failed\n");
		return -1;
	}

	/* get the master file system and check to see if there are differences */
	int master_len = 0;
	while (NULL == (master = recv_master(cnt, &master_len))){
		printf("SendMasterFSRequest: recv_master returned NULL, sleeping\n");
		sleep(1);
	}
	printf("Received master file system\n");

	/* set root of returned file system */
	filesystem_set_root_path(master, dartsync_dir);

	/* todo -- should also receive master file table */
	int master_ft_len = 0;
	while (NULL == (ft = recv_master_ft(cnt, &master_ft_len))){
		printf("SendMasterFTRequest: recv_master_ft return NULL, sleeping\n");
		sleep(1);
	}
	printf("Received master file table\n");
	filetable_print(ft);

	UpdateLocalFilesystem(master);
	return 1;
}

void UpdateLocalFilesystem(FileSystem *new_fs){
	printf("UpdateLocalFilesystem: received update from master!\n");

	/* received the master file system, iterate over it to look for differences */
	FileSystem *additions;
	FileSystem *deletions;
	filesystem_diff(cur_fs, new_fs, &additions, &deletions);

	if (!deletions){
		printf("UpdateLocalFilesystem: deletions is NULL\n");
	} else if (-1 == CheckFileSystem(deletions)){
		printf("UpdateLocalFilesystem: deletions is empty\n");
	} else {
		/* iterate over additions to see if we need to request files */
		/* delete any files that we need to update, or remove flat out */

		// need to to this to remove existing, pending requests even though we will reset
		// the current file system at the end of RemoveFileDeletions
		filetable_remove_filesystem(ft, deletions);

		// MATT FLAG -- should blow out pending work requests
		RemoveFileDeletions(deletions);
	}

	/* iterate over what I have left, and let the tracker know which chunks I have */
	FileSystemIterator *fs_iterator = filesystemiterator_relative_new(cur_fs, 0);
	if (!fs_iterator){
		printf("UpdateLocalFilesystem: failed to create relative iterator for cur_fs\n");
		return;
	} else if (-1 == CheckFileSystem(cur_fs)){
		printf("UpdateLocalFilesystem: no local files remaining after deletions\n");
	} else {
		printf("UpdateLocalFilesystem: we have local files, need to let tracker know what we have\n");
		char *path;
		int len;
		time_t mod_time;
		while (NULL != (path = filesystemiterator_next(fs_iterator, &len, &mod_time))){
			int num_chunks = num_chunks_for_size(len);
			if (num_chunks < 1){
				printf("UpdateLocalFilesystem: no chunks for file\n");
				continue;
			}
			if (len == -1){
				printf("UpdateLocalFilesystem: directory. don't send\n");
				continue;
			}
			for (int i = 0; i < num_chunks; i++){
				send_chunk_got(cnt, path, i);
			}
		}
	}
	filesystemiterator_destroy(fs_iterator);

	if (!additions){
		printf("UpdateLocalFilesystem: additions is NULL\n");
	} else if (-1 == CheckFileSystem(additions)){
		printf("UpdateLocalFilesystem: additions is empty\n");
	} else {
		/* iterate over additions to see if we need to request files */
		printf("UpdateLocalFilesystem: ready to iterate over additions!\n");
		FileSystemIterator* add_iterator = filesystemiterator_relative_new(additions, 0);

		if (!add_iterator){
			printf("UpdateLocalFilesystem: failed to make add iterator\n");
		}

		char *path;
		int len;
		time_t mod_time;
		while (NULL != (path = filesystemiterator_next(add_iterator, &len, &mod_time))){

			char *expanded_path = append_DSRoot(path, dartsync_dir);
			printf("UpdateLocalFilesystem: found addition at: %s\n", expanded_path);

			/* if addition is just a directory -> make that and then go onto files */
			if (len == -1) {

				if (-1 == mkdir(expanded_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
					printf("UpdateLocalFilesystem: failed to create directory \'%s\'\n", expanded_path);
					perror("Failed because");
				}
				continue;
			}

			/* re make that deleted file */
			ChunkyFile *file = chunkyfile_new_for_writing_to_path(expanded_path, len, mod_time);
			if (!file){
				printf("UpdateLocalFilesystem: chunkyfile_new_empty() failed()\n");
				continue;
			}
			//chunkyfile_write(file);

			// ADD CHUNKYFILE TO HASH TABLE!!!!!!!!!!
			filetable_set_chunkyfile(ft, path, file);

			/* get ready for next iteration */
			path = NULL;
			free(expanded_path);
			expanded_path = NULL;
		}

		/* MATT FLAG -- enqueue all of the chunks we need */
		filetable_enqueue_work_for_filesystem(ft, additions);

		filesystemiterator_destroy(add_iterator);
	}

	/* replace the old file system with the new one */
	filesystem_destroy(cur_fs);
	cur_fs = NULL;
	cur_fs = new_fs;

	/* cleanup */
	filesystem_destroy(additions);
	filesystem_destroy(deletions);
}

unsigned long microseconds_between(struct timeval* t0, struct timeval* t1)
{
	return (t1->tv_sec - t0->tv_sec) * 1000000 + (t1->tv_usec - t0->tv_usec);
}

void CheckLocalFilesystem()
{
	struct timeval now;
	gettimeofday(&now, NULL);
	if (microseconds_between(&prev_time, &now) < 5000000)
	{
		return;
	}
	prev_time = now;
	
	FileSystem *new_fs = filesystem_new(dartsync_dir);
	FileSystem *adds = NULL, *dels = NULL;

	if (!new_fs)
	{
		printf("CheckLocalFilesystem: filesystem_new() failed\n");
		return;
	}

	if (prev_fs == NULL || !filesystem_equals(prev_fs, new_fs))
	{
		printf("CheckLocalFilesystem: Local filesystem is changing. Waiting to settle before reporting diffs.\n");
		if (prev_fs)
		{
			filesystem_destroy(prev_fs);
		}
		prev_fs = new_fs;
		return;
	}

	filesystem_diff(cur_fs, new_fs, &adds, &dels);

	if (1 == CheckFileSystem(adds)) 
	{
		printf("CheckLocalFilesystem: Additions Seen ----\n");
		filesystem_print(adds);
	}

	if (1 == CheckFileSystem(dels)) 
	{
		printf("CheckLocalFilesystem: Deletions Seen -----\n");
		filesystem_print(dels);
	}

	/* if there are either additions or deletions, then we need to let the 
	 * master know */
	if (!adds && !dels)
	{
		printf("CheckLocalFilesystem: diff failed\n");
		filesystem_destroy(new_fs);
	} 
	else if ((1 == CheckFileSystem(adds)) || (1 == CheckFileSystem(dels)))
	{
		printf("CheckLocalFilesystem: about to send diffs to the tracker\n");
		/* send the difs to the tracker */
		if (-1 == send_updated_files(cnt, adds, dels))
		{
			printf("CheckLocalFilesystem: send_updated_files() failed\n");
		}

		// update the file table
		filetable_remove_filesystem(ft, dels);

		// MATT FLAG -- don't enqueue work requests
		filetable_add_filesystem(ft, adds, myID, HAVE_DATA);

		/* update the pointer to our *current* filesystem */
		filesystem_destroy(cur_fs);
		filesystem_destroy(adds);
		filesystem_destroy(dels);
		cur_fs = NULL;
		cur_fs = new_fs; 
	} 
	else 
	{
		/* else we have no diffs so just destroy the new fs we created */
		filesystem_destroy(new_fs);
		filesystem_destroy(adds);
		filesystem_destroy(dels);
	}
}

void DropFromNetwork(){
	/* call Matt's drop from network function */
	EndClientNetwork(cnt);

	/* close our files and free our memory */
	printf("DropFromNetwork: matt's memory is cleaned up, destroying mine\n");
	if (cur_fs)
		filesystem_destroy(cur_fs);
	if (ft)
		filetable_destroy(ft);

	//free(dartsync_dir);

	exit(0);
}

int RemoveFileDeletions(FileSystem *deletions){
	printf("RemoveFileDeletions: ready to iterate over deletions!\n");
	FileSystemIterator* del_iterator = filesystemiterator_new(deletions, 1);
	char *path;
	int len;
	time_t mod_time;
	
	if (!del_iterator){
		printf("RemoveFileDeletions: failed to make del iterator\n");
		return -1;
	}

	/* iterate over the fileystem, delete any files that are in it */
	while (NULL != (path = filesystemiterator_next(del_iterator, &len, &mod_time))){
		printf("RemoveFileDeletions: found deletion at: %s\n", path);
		if (-1 == remove(path)){
			printf("RemoveFileDeletions: remove() failed on path\n");
		}

		path = NULL;
	}

	printf("RemoveFileDeletions: done deleting\n");
	filesystemiterator_destroy(del_iterator);
	filesystem_destroy(cur_fs);
	cur_fs = NULL;
	cur_fs = filesystem_new(dartsync_dir);

	return 1;
}

int GetFileAdditions(FileSystem *additions, int author_id){
	printf("GetFileAdditions: ready to iterate over additions!\n");
	FileSystemIterator* add_iterator = filesystemiterator_relative_new(additions, 0);

	char *path;

	if (!add_iterator){
		printf("GetFileAdditions: failed to make add iterator\n");
		return -1;
	}

	/* iterate over the fileystem, delete any files that are in it */
	int len;
	time_t mod_time;
	while (NULL != (path = filesystemiterator_next(add_iterator, &len, &mod_time))){
		printf("GetFileAdditions: found addition at: %s\n", path);

		/* get the expanded path */
		char *expanded_path = append_DSRoot(path, dartsync_dir);

		/* if this is a folder */
		if (-1 == len){
			if (-1 == mkdir(expanded_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
				printf("GetFileAdditions: failed to create directory \'%s\'\n", path);
				perror("Failed because");
			}
			continue;
		}

		/* open chunk file and get the number of chunks */
		printf("Opening new chunky file at %s\n", expanded_path);
		ChunkyFile* file = chunkyfile_new_for_writing_to_path(expanded_path, len, mod_time);
		if (!file){
			printf("GetFileAdditions: failed to open new chunkyfile\n");
			continue;
		}
		//chunkyfile_write(file);
		
		/* write that file to the path */
		printf("chunky file write to path: %s\n", expanded_path);

		// ADD CHUNKYFILE TO HASH TABLE!!!!!
		filetable_set_chunkyfile(ft, path, file);
		
		int total_chunks = chunkyfile_num_chunks(file);
		if (total_chunks < 1) {
			fprintf(stderr,"chunkyfile %s has no chunks!\n", expanded_path);
			chunkyfile_write(file);
			continue;
		}

		path = NULL;
	}

	filesystemiterator_destroy(add_iterator);
	return 1;
}

/* ----------------------- Private Function Bodies --------------------- */

int CheckFileSystem(FileSystem *fs){
	char *path;
	int len;
	time_t mod_time;
	FileSystemIterator *iterator = filesystemiterator_new(fs,0);

	if (NULL != (path = filesystemiterator_next(iterator, &len, &mod_time))){
		printf("CheckFileSystem: FileSystem is nonempty\n");
		filesystemiterator_destroy(iterator);
		return 1;
	}
	//printf("CheckFileSystem: FileSystem is empty\n");
	filesystemiterator_destroy(iterator);
	return -1;
}

int check_work_queue(CNT * cnt, FileTable * ft) 
{
	if (!cnt || !ft) 
	{
		return -1;
	}

	int chunk, dest_id, job_id;
	FileTableEntry * fte;

	// scan file table
	FileTableIterator * fti = filetableiterator_new(ft);
	if (fti) 
	{
		/* iterate over all file table entries */
		while ( (fte = filetableiterator_next(fti)) != NULL) 
		{
			/* get jobs */
			while (filetableentry_get_job(fte, MAX_PENDING_REQUESTS, &chunk, &dest_id, &job_id) == 1) 
			{
				send_chunk_request(cnt, dest_id, fte->path, chunk, job_id);
			}

		}
		filetableiterator_destroy(fti);
	}
	return 1;
}


/* ------------------------------- main -------------------------------- */
int main(int argv, char* argc[]){

	gettimeofday(&prev_time, NULL);

	/* arg check */
	if ( !(argv == 2 || argv == 3) ){
		printf("CLIENT MAIN usage error: \'./client_app\' [IP ADDRESS] [optional... SYNC DIRECTORY]\n");
		exit(0);
	} 

	char ip_addr[sizeof(struct in_addr)]; 	// IPv4 address length
	if (inet_pton(AF_INET, argc[1], ip_addr) != 1) {
		fprintf(stderr,"Could not parse host %s\n", argc[1]);
		exit(-1);
	}

	/* initialize the psuedorandom number generator */
	srand(time(NULL));

	/* start the client connection to the tracker and network */
	/* may eventually update to add password sending */
	if (NULL == (cnt = StartClientNetwork(ip_addr, sizeof(struct in_addr)) ) ) {
		printf("CLIENT MAIN: StartClientNetwork failed\n");
		exit(-1);
	}

	/* catch sig int so that we can politely close networks on kill */
	signal(SIGINT, DropFromNetwork);
	
	/* set directory that will be used */
	if (argv == 3) {
		dartsync_dir = get_absolute_root(argc[2]);
		if (!dartsync_dir) {
			fprintf(stderr,"Failed to get absolute root path for %s\n",argc[2]);
			exit(-1);
		}
	} else {
		dartsync_dir = get_absolute_root("~/dart_sync");
		if (!dartsync_dir) {
			fprintf(stderr, "Failed to get absolute root path for \'~/dart_sync\'\n");
			exit(-1);
		}
	}

	/* get the current local filesystem */
	if (NULL == (cur_fs = filesystem_new(dartsync_dir))){
		printf("CLIENT MAIN: filesystem_new() failed\n");
		filetable_destroy(ft);
		exit(-1);
	}
	filesystem_print(cur_fs);

	/* send a request to the tracker for the master filesystem. 
	 * this will check against our current filesystem and make requests for updates */
	SendMasterFSRequest(cur_fs);

	/* start the loop process that will run while we are connected to the tracker 
	 * this will handle peer adds and dels and receive updates from the master 
	 * filesystem and monitor the local filesystem */
	int peer_id = -1, chunk_id = -1, len = -1, request_id = -1;
	FileSystem *master;
	int recv_len;
	filesystem_print(cur_fs);
	filetable_print(ft);
	while (1){
		/* if this doesn't get changed down the loop, then it will wait for this time */
		sleep_time = POLL_STATUS_DIFF_LONG;

		/* get any new clients first */
		// printf("CLIENT MAIN: checking for new clients\n");
		while (-1 != (peer_id = recv_peer_added(cnt))){
			printf("CLIENT MAIN: new peer %d\n", peer_id);
			peer_id = -1;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* figure out if any clients have dropped from the network */
		// printf("CLIENT MAIN: checking for deleted clients\n");
		while (-1 != (peer_id = recv_peer_deleted(cnt))){
			printf("CLIENT MAIN: deleting peer %d\n", peer_id);
			/* delete from file table */
			filetable_remove_peer(ft, peer_id);
			peer_id = -1;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* check to see if any requests for files have been made to you */
		// printf("CLIENT MAIN: checking for chunk requests\n");
		char *filepath;
		while (-1 != receive_chunk_request(cnt, &peer_id, &filepath, &chunk_id, &request_id)){
			printf("CLIENT MAIN: received chunk request from peer: %d\n", peer_id);

			printf("appending %s, %s\n", filepath, dartsync_dir);
			// char* root_path = filesystem_get_root_path(cur_fs);
			char *expanded_path = append_DSRoot(filepath, dartsync_dir);

			/* get the chunk that they are requesting */
			ChunkyFile *file = filetable_get_chunkyfile(ft, filepath);
			
			if (!file)
			{
				file = chunkyfile_new_for_reading_from_path(expanded_path);

				if (!file)
				{	
					printf("chunkyfile_new_for_reading_from_path() failed on %s\n", expanded_path);

					/* send an error response */
					send_chunk_rejection(cnt, peer_id, filepath, chunk_id, request_id);
					peer_id = -1;
					continue;
				}
				
				filetable_set_chunkyfile(ft, filepath, file);
			}

			printf("CLIENT MAIN: sending chunk (%s, %d) to peer %d", expanded_path, chunk_id, peer_id);

			char *chunk_text;
			int chunk_len;
			chunkyfile_get_chunk(file, chunk_id, &chunk_text, &chunk_len);

			/* send that chunk to the peer */
			send_chunk(cnt, peer_id, filepath, chunk_id, chunk_text, chunk_len, request_id);

			/* destroy that chunky file */
			//chunkyfile_destroy(file);

			peer_id = -1;
			filepath = NULL;
			free(expanded_path);
			expanded_path = NULL;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* poll for any received chunks */
		// printf("CLIENT MAIN: checking for chunks received\n");
		char *chunk_data;
		int chunk_id;
		while (-1 != receive_chunk(cnt, &peer_id, &filepath, &chunk_id, &len, &chunk_data, &request_id)){
			char *expanded_path = append_DSRoot(filepath, dartsync_dir);
			printf("CLIENT MAIN: received chunk %d for file %s (expanded: %s\n", chunk_id, filepath, expanded_path);

			// MATT FLAG -- DOES REQUEST MATCH AN OUTSTANDING REQUEST ID?
			if (filetable_find_and_remove_job_id(ft, filepath, request_id) != request_id) {
				printf("CLIENT MAIN: ignoring received chunk (File: %s, Id: %d)\n", filepath, request_id);

				free(filepath);
				if (len != -1) {
					free(chunk_data);
				}
				continue;
			}

			// GET THE CHUNKYFILE FROM THE HASH TABLE!!!!!
			ChunkyFile* file = filetable_get_chunkyfile(ft, filepath);
			if (!file){
				printf("CLIENT MAIN: failed to get chunkfile from ft on receive_chunk\n");
				continue;
			}

			/* if len is -1, then we received a rejection response */
			if (-1 == len){	// how should I handle this???
				printf("CLIENT MAIN: receive_chunk got a rejection response\n");

				// MATT FLAG -- REQUEUE THE REQUEST
				filetable_enqueue_work_request(ft, filepath, chunk_id);

				free(filepath);
				continue;
			}

			/* set the correct chunk */
			chunkyfile_set_chunk(file, chunk_id, chunk_data, len);

			// SEND THE FILE ACQ TO THE TRACKER
			send_chunk_got(cnt, filepath, chunk_id);

			if (chunkyfile_all_chunks_written(file)){
	
				chunkyfile_write(file);
				/* destroy the chunky file */
				chunkyfile_destroy(file);
				filetable_set_chunkyfile(ft, filepath, NULL);
			}

			/* destroy the old filesystem struct and recreate it to reflect changes */
			filesystem_destroy(cur_fs);
			cur_fs = NULL;
			cur_fs = filesystem_new(dartsync_dir);

			free(filepath);
			free(chunk_data);
			peer_id = -1;
			filepath = NULL;
			chunk_data = NULL;
			free(expanded_path);
			expanded_path = NULL;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* check for chunk aquisition updates and add them to our file table */
		while (-1 != receive_chunk_got(cnt, &peer_id, &filepath, &chunk_id)){
			char *expanded_path = append_DSRoot(filepath, dartsync_dir);
			printf("CLIENT MAIN: received chunk acq update from %d on file %s chunk %d\n", 
					peer_id, expanded_path, chunk_id);

			filetable_set_that_peer_has_file_chunk(ft, filepath, peer_id, chunk_id);
			peer_id = -1;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* poll for any diffs, if they exist then we need to make updates to 
		 * our filesystem */
		// printf("CLIENT MAIN: checking for diffs\n");
		FileSystem *additions, *deletions;
		while (-1 != recv_diff(cnt, &additions, &deletions, &peer_id)){
			printf("CLIENT MAIN: received a diff from author %d\n", peer_id);
			/* set the root paths of the additions and deletions filesystems */
			filesystem_set_root_path(additions, dartsync_dir);
			filesystem_set_root_path(deletions, dartsync_dir);

			/* get rid of the deletions */
			RemoveFileDeletions(deletions);

			/* update current fs to reflect the deletions */
			filesystem_minus_equals(cur_fs, deletions);

			/* remove them from the file table */
			filetable_remove_filesystem(ft, deletions);

			/* update current file with additions (time stamped) */
			filesystem_plus_equals(cur_fs, additions);

			/* the originator of this diff has the master, so tell the filetable */
			// MATT FLAG -- ENQUEUE ALL WORK REQUESTS
			filetable_add_filesystem(ft, additions, peer_id, NEED_DATA);

			/* get the file additions from peers, and update the filesystem */
			GetFileAdditions(additions, peer_id);

			/* update the current filesystem pointer */
			filesystem_destroy(cur_fs);
			cur_fs = NULL;
			cur_fs = filesystem_new(dartsync_dir);

			/* get ready for next recv diff or iteration of loop */
			filesystem_destroy(deletions);
			filesystem_destroy(additions);
			additions = NULL;
			deletions = NULL;
			peer_id = -1;
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* poll to see if there are any changes to the master file system 
		 * if there are then we need to get those parts from peers and then
		 * copy master to our local pointer of the filesystem */
		// printf("CLIENT MAIN: checking for updates from master\n");
		while (NULL != (master = recv_master(cnt, &recv_len))){

			printf("CLIENT MAIN: received master from tracker\n");
			/* set the root path of the master filesystem */
			filesystem_set_root_path(master, dartsync_dir);

			filesystem_print(master);

			UpdateLocalFilesystem(master);
			sleep_time = POLL_STATUS_DIFF_SHORT;
		}

		/* MATT FLAG -- DO REQUEST WORK HERE */
		check_work_queue(cnt, ft);

		/* check the local filesystem for changes that we need to push
		 * to master */
		// printf("CLIENT MAIN: checking local file system\n");
		CheckLocalFilesystem();

		/* sleep for the dynamic amount of time */
		usleep(sleep_time);
	}
}



