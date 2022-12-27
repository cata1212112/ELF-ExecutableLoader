/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "exec_parser.h"

static so_exec_t *exec;
static char* exec_path;
static int PAGESIZE;
static struct sigaction old_action;

int max(int a, int b) {
	return (a < b) ? b : a;
}

int min(int a, int b) {
	return a + b - max(a,b);
}

typedef struct pagina{
	unsigned int pg_num;
	struct pagina* next;
} page;

so_seg_t* find_segment_of_address(uintptr_t addr) {
	for (int i=0; i<exec->segments_no; i++) {
		if (addr >= exec->segments[i].vaddr && addr < exec->segments[i].vaddr + exec->segments[i].mem_size) {
			return &exec->segments[i];
		}
	}
	return NULL;
}

void copy_from_exec_to_page(so_seg_t *segment, char *exec_path, char *page, uintptr_t addr) {
	unsigned int num_page = (addr - segment->vaddr) / PAGESIZE;
	int exec_fd = open(exec_path, O_RDONLY);
	lseek(exec_fd, segment->offset + num_page * PAGESIZE, SEEK_SET);
	char *buffer = calloc(PAGESIZE, sizeof(char));
	int rd = read(exec_fd, buffer, min(PAGESIZE, max(0,segment->file_size - num_page * PAGESIZE)));
	memcpy(page, buffer, rd);
	close(exec_fd);
	free(buffer);
}

void* find_page(page *linked_list, unsigned int pg_num) {
	while (linked_list != NULL) {
		if (linked_list->pg_num == pg_num) {
			return linked_list;
		}
		linked_list = linked_list->next;
	}
	return NULL;
} 

page* new_page(unsigned int pg_num) {
	page* aux = malloc(sizeof(page));
	aux->pg_num = pg_num;
	aux->next = NULL;
	return aux;
}

void insert_page(page *linked_list, unsigned int pg_num) {
	if (linked_list == NULL) {
		linked_list = new_page(pg_num);
	} else {
		while (linked_list->next == NULL) linked_list = linked_list -> next;
		
		linked_list -> next = new_page(pg_num);
	}
}

static void segv_handler(int signum, siginfo_t *info, void *context){
	if (info->si_code == SEGV_ACCERR) {
		old_action.sa_sigaction(signum, info, context);
	}
	so_seg_t* segment = find_segment_of_address((uintptr_t)info->si_addr);
	if (segment == NULL) {
		old_action.sa_sigaction(signum, info, context);
	} else {
		unsigned int offset_v_addr = (uintptr_t)info->si_addr - segment->vaddr;
		unsigned int current_page = offset_v_addr / PAGESIZE;
		page* pg = find_page(segment->data, current_page);
		if (pg != NULL) {
			old_action.sa_sigaction(signum, info, context);
		}
			
		void *page = mmap((void*)segment->vaddr + current_page * PAGESIZE, PAGESIZE, PROT_WRITE, MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
		copy_from_exec_to_page(segment, exec_path, page, (uintptr_t)info->si_addr);
		mprotect(page, PAGESIZE, segment->perm);
		insert_page(segment->data, current_page);
		
	}
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;
	PAGESIZE = getpagesize();
	
	
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, &old_action);
	if (rc < 0) {
		perror("sigaction");
		return -1;
	}
	return 0;
}

int so_execute(char *path, char *argv[]){
	exec_path = path;
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	so_start_exec(exec, argv);
	

	return -1;
}
