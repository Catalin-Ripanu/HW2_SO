// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

/* O functie care reface lista simplu inlantuita */
void restoration(void)
{
	last_free_block = first_free_block;
	if (last_free_block) {
		while (last_free_block->next)
			last_free_block = last_free_block->next;
	}
}

/* O functie pentru a construi un nou bloc pe baza concatenarii */
int merge_block(struct block_meta *prev_block, struct block_meta *next_block)
{
	/* Concatenarea se realizeaza doar atunci cand cele 2 blocuri sunt libere */
	if (prev_block && prev_block->status == STATUS_FREE) {
		if (next_block && next_block->status == STATUS_FREE) {
			prev_block->next = next_block->next;

			/* Se calculeaza noua dimensiune a blocului construit */
			prev_block->size += next_block->size + ALIGN(sizeof(struct block_meta));
			return 1;
		}
	}
	return 0;
}

/* Functia care implementeaza cautarea celui mai bun bloc de memorie */
/* Cautarea se face avand in vedere ca blocul selectat sa fie liber */
struct block_meta *get_free_block(size_t size)
{
	struct block_meta *current = first_free_block;
	struct block_meta *best_block = NULL;
	int min_fit_size = INT_MAX;

	while (current != NULL) {

		/* Daca marimea blocului curent este mai mare decat marimea dorita */
		if (current->status == STATUS_FREE &&
			current->size >= size && current->size < (size_t)min_fit_size) {
		/* Se efectueaza salvarea adresei blocului */
			best_block = current;
			min_fit_size = current->size;
		}
		current = current->next;
	}

	/* Daca nu s-a gasit un bloc liber*/
	if (!best_block && last_free_block) {
		/* Se verifica daca se poate mari dimensiunea ultimului bloc*/
		if (last_free_block->status == STATUS_FREE)
			best_block = last_free_block;
	}
	return best_block;
}

/* Functia care se ocupa de descompunerea unui bloc dat*/
void split_block(struct block_meta *block_to_split, size_t req_size)
{
	/* Se calculeaza noua dimensiune */
	size_t new_size = block_to_split->size - req_size;

	if (new_size <= 0)
		return;

	/* Daca marimea ramasa este acceptabila */
	if (new_size >= ALIGN(sizeof(struct block_meta)) + 1) {
		struct block_meta *new_block = (struct block_meta *)((void *)block_to_split + req_size + ALIGN(sizeof(struct block_meta)));

		/* Se formeaza restul blocului */
		new_block->size = new_size - ALIGN(sizeof(struct block_meta));
		new_block->status = STATUS_FREE;
		new_block->next = block_to_split->next;
		block_to_split->size = req_size;
		block_to_split->status = STATUS_ALLOC;
		block_to_split->next = new_block;
	}
}

/* Functia care foloseste apelul sbrk() pentru a aloca memorie pe heap*/
struct block_meta *create_new_block(size_t new_size)
{
	struct block_meta *new_block = (struct block_meta *)sbrk(new_size);

	DIE(new_block == (void *)-1, "sbrk failed");
	new_block->size = new_size - ALIGN(sizeof(struct block_meta));
	new_block->status = STATUS_ALLOC;
	new_block->next = NULL;
	return new_block;
}

/* Functia care formeaza un nou bloc folosind apelul mmap() */
struct block_meta *create_special_mmap_block(size_t size, size_t limit)
{
	restoration();
	if (size + ALIGN(sizeof(struct block_meta)) >= limit) {
		struct block_meta *block = mmap(NULL, size + ALIGN(sizeof(struct block_meta)),
										PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		no_mmap_alloc++;
		DIE(block == MAP_FAILED, "mmap failed");
		block->size = size;
		block->status = STATUS_MAPPED;
		block->next = NULL;

		/* Partea care se ocupa cu adaugarea in lista de blocuri */
		if (!first_free_block) {
			first_free_block = block;
			last_free_block = block;
		} else {
			last_free_block->next = block;
			last_free_block = last_free_block->next;
			last_free_block->next = NULL;
		}
		return block + 1;
	} else
		return NULL;
}

/* Functia care implementeaza malloc-ul si toate proprietatile sale*/
void *os_malloc(size_t size)
{
	if (size <= 0)
		return NULL;
	size_t req_size = ALIGN(size);

	/* Se foloseste mmap() daca size-ul depaseste pragul MMAP_TH*/
	struct block_meta *new_block = create_special_mmap_block(req_size, MMAP_TH);

	if (new_block)
		return new_block;

		/* Daca s-a produs macar o alocare pe heap in trecut */
	if (first_free_block && no_brk_alloc) {

		/* La fiecare cautare se concateneaza blocurile adiacente libere */
		struct block_meta *prev_block = first_free_block;
		struct block_meta *next_block = first_free_block->next;

		while (next_block) {
			int res = merge_block(prev_block, next_block);

			if (res) {
				next_block = next_block->next;
			} else {

				prev_block = next_block;
				next_block = next_block->next;
			}
		}
		last_free_block = first_free_block;
		while (last_free_block->next)
			last_free_block = last_free_block->next;

		/* Se cauta un posibil candidat */
		new_block = get_free_block(req_size);

		/* Daca rezultatul are aceeasi dimensiune cu dimensiunea dorita */
		if (new_block && new_block->status == STATUS_FREE && new_block->size == req_size) {

			/* Se returneaza direct blocul */
			new_block->status = STATUS_ALLOC;
			return new_block + 1;

		/* Daca rezultatul are dimensiunea mai mare decat dimensiunea dorita */
		} else if (new_block && new_block->status == STATUS_FREE && new_block->size > req_size) {

			/* Se produce operatia de split pentru a gestiona memoria in mod eficient */
			split_block(new_block, req_size);
			new_block->status = STATUS_ALLOC;
			return new_block + 1;

		/* Daca ultimul bloc de memorie este liber */
		} else if (new_block == last_free_block && last_free_block->status == STATUS_FREE) {

			/* Se efectueaza operatia de extindere in cazul acestui bloc */
			size_t rem_size = req_size - last_free_block->size;
			struct block_meta *exp_block = (struct block_meta *)sbrk(rem_size);

			DIE(exp_block == (void *)-1, "sbrk failed");
			last_free_block->size += rem_size;
			last_free_block->next = NULL;
			last_free_block->status = STATUS_ALLOC;
			return last_free_block + 1;
		}

		/* Daca nu s-a gasit niciun candidat in urma cautarii */
		/* Se aloca un nou bloc pe heap cu marimea dorita */
		new_block = create_new_block(req_size + ALIGN(sizeof(struct block_meta)));
		last_free_block->next = new_block;
		last_free_block = last_free_block->next;
		last_free_block->next = NULL;
		return new_block + 1;

		/* Daca nu s-au mai produs alocari pe heap in trecut */
	} else if (!no_brk_alloc) {

		/* Se foloseste operatia de prealocare de memorie pe heap */
		new_block = create_new_block(MMAP_TH);
		no_brk_alloc++;
		/* Se introduce blocul de memorie, in lista, in mod corespunzator */
		if (!no_mmap_alloc) {
			first_free_block = new_block;
			last_free_block = new_block;
		} else {
			first_free_block->next = new_block;
			last_free_block->next = new_block;
			last_free_block = last_free_block->next;
			last_free_block->next = NULL;
		}
		return new_block + 1;
	} else
		return NULL;
}

/* Functia care implementeaza functia free() cu proprietatile aferente */
void os_free(void *ptr)
{
	if (!ptr)
		return;
	char exists = 0;
	struct block_meta *tmp = first_free_block;

	/* Se verifica daca adresa este valida */
	/* Altfel spus, se cauta in lista blocul de memorie de interes */
	while (tmp) {
		if (tmp == (struct block_meta *)ptr - 1) {
			exists = 1;
			break;
		}
		tmp = tmp->next;
	}

	/* Daca blocul de interes a fost gasit */
	if (exists) {
		struct block_meta *del_block = (struct block_meta *)ptr - 1;

		/* Se trateaza cazul in care acesta este deja liber */
		if (del_block->status == STATUS_FREE)
			return;

		/* Variabila tmp reprezinta blocul anterior */
		/* Cu alte cuvinte, del_block->prev == tmp */
		tmp = first_free_block;
		while (tmp) {
			if (tmp->next == del_block)
				break;
			tmp = tmp->next;
		}

		/* Daca blocul este alocat pe heap */
		if (del_block->status == STATUS_ALLOC) {

			/* Se marcheaza ca fiind liber */
			del_block->status = STATUS_FREE;
			int res = merge_block(tmp, del_block);

			if (res)
				merge_block(tmp, tmp->next);
			else
				merge_block(del_block, del_block->next);

		/* Daca blocul este construit folosind mmap() */
		} else {
			int var = 0;

			/* Se sterge blocul in mod corespunzator */
			if (del_block != first_free_block && del_block != last_free_block)
				tmp->next = del_block->next;
			if (del_block == first_free_block) {
				first_free_block = NULL;
				last_free_block = NULL;
			}
			if (del_block == last_free_block) {
				last_free_block = tmp;
				var = 1;
			}

			/* Se apeleaza munmap() pentru eliminare */
			int res = munmap(del_block, del_block->size + ALIGN(sizeof(struct block_meta)));

			DIE(res == -1, "munmap failed");
			if (var)
				last_free_block->next = NULL;
		}
	}
}

/* Functia care implementeaza calloc() cu proprietatile aferente */
void *os_calloc(size_t nmemb, size_t size)
{
	if (!nmemb || !size)
		return NULL;
	size_t req_size = ALIGN(nmemb * size);

	/* Se verifica daca marimea dorita depaseste dimensiunea unei pagini */
	struct block_meta *new_block = create_special_mmap_block(req_size, PAGE_SIZE);

	if (new_block)
		return new_block;
	void *addr = os_malloc(req_size);

	if (addr)
		memset(addr, 0, req_size);
	return addr;
}

/* Functia care implementeaza realloc() cu proprietatile aferente */
void *os_realloc(void *ptr, size_t size)
{
	size_t req_size = ALIGN(size);

	/* Se trateaza cazurile de baza */
	if (ptr == NULL)
		return os_malloc(req_size);
	if (req_size == 0) {
		os_free(ptr);
		return NULL;
	}
	struct block_meta *curr_block = (struct block_meta *)ptr - 1;

	if (curr_block->status == STATUS_FREE)
		return NULL;
	struct block_meta *tmp = first_free_block;

	while (tmp) {
		if (tmp->next == curr_block)
			break;
		tmp = tmp->next;
	}

	/* Daca blocul a fost alocat pe heap */
	if (curr_block->status == STATUS_ALLOC) {
		size_t new_size = req_size;

		/* Se aplica trunchierea daca marimea noua este mai mica */
		if (curr_block->size >= req_size) {
			if (curr_block->size - new_size >= sizeof(struct block_meta) + 1) {
				struct block_meta *new_block = (struct block_meta *)(ptr + new_size);

				new_block->status = STATUS_FREE;
				new_block->size = curr_block->size - new_size - sizeof(struct block_meta);
				new_block->next = curr_block->next;
				curr_block->status = STATUS_ALLOC;
				curr_block->next = new_block;
				curr_block->size = new_size;
			}
			return ptr;
		}

		/* Se aloca un nou bloc daca marimea noua depaseste pragul MMAP_TH */
		if (new_size >= MMAP_TH + ALIGN(sizeof(struct block_meta))) {
			struct block_meta *new_block = os_malloc(new_size);

			memcpy(new_block, ptr, new_size < curr_block->size ? new_size : curr_block->size);
			os_free(ptr);
			return new_block;
		}
		struct block_meta *next_block = curr_block->next;

		/* Se lipesc toate blocurile libere adiacente pentru extindere */
		while (next_block && next_block->status == STATUS_FREE) {
			curr_block->next = next_block->next;
			curr_block->size += ALIGN(sizeof(struct block_meta)) + next_block->size;
			if (curr_block->size >= new_size) {

				/* Se verifica, suplimentar, daca se poate face un split() */
				if (curr_block->size - new_size >= ALIGN(sizeof(struct block_meta)) + 1) {
					struct block_meta *new_block = (struct block_meta *)((void *)curr_block +
																		 new_size + ALIGN(sizeof(struct block_meta)));

					new_block->size = curr_block->size - new_size - ALIGN(sizeof(struct block_meta));
					new_block->status = STATUS_FREE;
					new_block->next = curr_block->next;
					curr_block->size = new_size;
					curr_block->status = STATUS_ALLOC;
					curr_block->next = new_block;
				}
				return ptr;
			}
			next_block->next = NULL;
			next_block = curr_block->next;
		}

		/* Daca blocul curent este chiar ultimul, se produce extinderea */
		if (curr_block == last_free_block) {
			size_t rem_size = new_size - curr_block->size;
			struct block_meta *exp_block = (struct block_meta *)sbrk(rem_size);

			DIE(exp_block == (void *)-1, "sbrk failed");
			curr_block->size += rem_size;
			curr_block->next = NULL;
			curr_block->status = STATUS_ALLOC;
			return curr_block + 1;
		}

		/* Altfel, se aloca un nou bloc, se copiaza datele si se sterge blocul vechi */
		struct block_meta *new_block = os_malloc(new_size);

		memcpy(new_block, ptr, new_size < curr_block->size ? new_size : curr_block->size);
		os_free(ptr);
		return new_block;

	/* Daca blocul a fost conceput folosind mmap() */
	} else if (curr_block->status == STATUS_MAPPED) {

		/* Se aloca memorie pentru un nou bloc ce se adauga in lista */
		struct block_meta *new_block = os_malloc(req_size);

		memcpy(new_block, ptr, req_size < curr_block->size ? req_size : curr_block->size);
		os_free(ptr);
		return new_block;
	}
	return NULL;
}
