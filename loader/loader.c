/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <math.h>

#include "exec_parser.h"

static struct sigaction old_action;

typedef struct seg_data{ // structura auxiliara care retine statusul maparii fiecarei pagini dintr-un segment
	bool* mapped;
}seg_data;

static so_exec_t *exec;
static int page_size;
char* file_path;

ssize_t xread(int fd, int offset, void* buf, size_t count){ //functie de read
//care citeste exact count bytes dintr-un fisier de la un anumit offset
	
	size_t bytes_read = 0;
	lseek(fd,offset,SEEK_SET);

	while(bytes_read < count){
		
		ssize_t bytes_read_now = read(fd,buf+bytes_read,count-bytes_read);

		if(bytes_read == 0)
			return bytes_read;
		
		if(bytes_read < 0)
			return -1;
		
		bytes_read += bytes_read_now;
	}

	close(fd);
	return bytes_read;
}

static void segv_handler(int signum, siginfo_t *info, void* context){

	if(signum != SIGSEGV){ //daca semnalul nu este un seg fault, se ruleza handler-ul default
		old_action.sa_sigaction(signum,info,context);
		return;
	}

	char* addr = (char*) info->si_addr;
	int ok = 0;


	for(int i = 0; i < exec->segments_no; i++){ // cautam segmentul care a generat fault-ul

		uintptr_t seg_addr = exec->segments[i].vaddr;

		if((uintptr_t)addr >= seg_addr && 
		(uintptr_t)addr <= seg_addr + exec->segments[i].mem_size){//daca adresa se afla  intre
		//adresa de inceput a segmentului si sfarsitul size-ului sau
			
			ok =1; // variabila folosita pentru a marca ca segmentul a fost gasit

			int page_index = ((uintptr_t)(addr - seg_addr)) / page_size; // determinam a cata pagina este cea cautata

			seg_data* data = (seg_data*) exec->segments[i].data;
			
			if(data->mapped[page_index] == 0){ // daca pagina nu este mapata
				
				int left_to_read = 0;

				if(page_index * page_size > exec->segments[i].file_size) // verificam daca mai este ceva de citit din fisier
					left_to_read = 0;
				else
					left_to_read = page_size <  // daca este mai putin de o pagina, citim o pagina, daca nu, citim cat a ramas
					page_size - ((page_index + 1)*page_size - exec->segments[i].file_size) ?
					page_size : page_size - ((page_index + 1)*page_size - exec->segments[i].file_size);
			

				char* buffer = malloc(page_size * sizeof(char)); // buffer in care se vor citi datele


				int offset = exec->segments[i].offset + page_index * page_size; // calculam offsetul din fisier
				int fd = open(file_path,O_RDONLY);

				if(left_to_read > 0)
					xread(fd,offset,buffer,left_to_read); // citim exact left_to_read bytes
				
				if(left_to_read < page_size) // daca este mai putin de o pagina
					memset((void*)(buffer+left_to_read), 0, page_size-left_to_read); // zero-izam restul paginii

				uintptr_t page_addr = seg_addr + page_index * page_size;

				char* rc = mmap((void*)page_addr,page_size, PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				//mapam pagina cu permisiuni de scriere
			
				if(rc == MAP_FAILED)
					exit(EXIT_FAILURE);

				memcpy(rc,buffer,page_size);// copiem datele din buffer in zona mapata

				int rc2 = mprotect(rc,page_size,exec->segments[i].perm); //setam permisiunile zonei mapate cu cele ale segmentului

				if(rc2 == -1)
					exit(EXIT_FAILURE);
				

				free(buffer);
				
				data->mapped[page_index] = 1; //marcam zona ca fiind mapata 

			}
			else{ // daca zona este mapata, rulam handler-ul default
				old_action.sa_sigaction(signum,info,context);
				return;
			}

			break;
		}
	}

	if(ok == 0){ // daca segmentul nu a fost gasit, rulam handler-ul default
		old_action.sa_sigaction(signum,info,context);
		return;
	}
}

int so_init_loader(void) // functia de initializare preluata din laborator
{
	/* TODO: initialize on-demand loader */
	struct sigaction action;
	int rc;

	action.sa_sigaction = segv_handler; 
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, SIGSEGV);
	action.sa_flags = SA_SIGINFO;

	rc = sigaction(SIGSEGV, &action, &old_action);

	if(rc == -1)
		exit(EXIT_FAILURE);

	return 0;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;
	
	page_size = getpagesize(); 

	file_path = calloc(80,sizeof(char));
	strcpy(file_path,path);

	for(int i = 0; i < exec->segments_no; i++){ //Pentru fiecare segment din executabil

		int nr_pages = ceil(exec->segments[i].mem_size / page_size); // calculeam numarul de pagini,

		seg_data* aux = malloc(sizeof(seg_data)); // creeam o structura ce contine un vector de bool
		aux->mapped = calloc(nr_pages,sizeof(bool)); // care indica daca pagina este sau nu mapata
		//initial, nici o pagina nu este mapata

		exec->segments[i].data = aux; // salvam structura in campul data segmentului
	}

	so_start_exec(exec, argv);

	return 0;
}
