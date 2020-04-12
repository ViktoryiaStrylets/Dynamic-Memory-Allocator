
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
#define MIN_BLOCK_SIZE (32)
#define  ALIGNMENT  16
/* round to the nearest multiple of alignment*/
#define  ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define  ADJ_SIZE (ALIGN(sizeof(size_t)))
#define  BLCK_HDR_SIZE (ALIGN(sizeof(sf_header)))
#define PROLOGUE_HDR (ALIGN(sizeof(sf_prologue)))



sf_free_list_node *find_position_for_newsize(size_t size);
sf_free_list_node *find_fit_for_size(size_t size);
sf_free_list_node *find_free_list(size_t size);
sf_header *sf_remove_free_node(sf_header *head);
void sf_add_free_node(sf_header *head, sf_header *newNode);
sf_header *place(sf_header *hp,size_t size);
sf_header * coalesceFreeBlock(sf_header *oldBlock);


void *sf_malloc(size_t size) {
  sf_errno=0;
  size_t asize; /* Adjusted block size*/
  sf_free_list_node * listPtr=NULL;
  sf_header *hp=NULL;
  sf_footer * footer=NULL;
  size_t blockSize=0;
  /*ignore sourious requests*/
  if(size==0)
    return NULL;

  asize=sizeof(sf_header)+size;
  /*adjust the size for the alignment requrements*/
  if(asize%16!=0)
     asize=ALIGN(asize);


  /* dont allocate any block less than minimum size*/
  if(asize<MIN_BLOCK_SIZE)
  {
    sf_errno=ENOMEM;
    return NULL;
  }


  /*find the list for the proper size */
  if((listPtr=find_fit_for_size(asize))!=NULL){
     hp=sf_remove_free_node(&(listPtr->head));
     hp=place(hp,size);
     return ((void*)hp+sizeof(sf_header));
                                              }
  /*no fit found. Get more memory and place the block;*/
  else{
       while(blockSize<asize){
          /*call the mem_grow function for new page,*/

          /*case 1 the mem_grow called for the first time*/
         if((sf_free_list_head.next==&sf_free_list_head)&&(sf_free_list_head.prev==&sf_free_list_head)){
             sf_prologue * prol=NULL;
             prol=(sf_prologue*)(sf_mem_grow());
         /*if no more memory available return null*/
            if(prol==NULL){
              sf_errno=ENOMEM;
              return NULL;
              }
        /*set up the header of the proloque*/
        prol->header.info.allocated=1;
        prol->header.info.prev_allocated=0;
        prol->header.info.block_size=0>>4;
        prol->header.info.requested_size=0;
    
       /*set up the footer of the proloque*/
       prol->footer.info.allocated=1;
       prol->footer.info.prev_allocated=0;
       prol->footer.info.block_size=0>>4;
       prol->footer.info.requested_size=0;

       sf_epilogue *epil=NULL;
       char * end=NULL;
       end=sf_mem_end();
       epil=(sf_epilogue *)(end-sizeof(sf_epilogue));
       epil->footer.info.allocated=1;
       epil->footer.info.prev_allocated=0;
       epil->footer.info.block_size=0>>4;
       epil->footer.info.requested_size=0;

      /*set up the header pointer;*/
      char* start=NULL;
      start=sf_mem_start();
      hp=(sf_header*) (start+sizeof(sf_prologue));
      hp->info.allocated=0;
      hp->info.prev_allocated=1;
      blockSize=(PAGE_SZ - sizeof(sf_prologue) - sizeof(sf_epilogue));
      hp->info.block_size=blockSize>>4;
      hp->info.requested_size=0;

      footer= (sf_footer *)(((char*)hp + blockSize) - sizeof(sf_footer));
      footer->info.allocated=0;
      footer->info.prev_allocated=1;
      footer->info.block_size=blockSize>>4;
      footer->info.requested_size=0;

}

else{
  hp=(sf_header*)(sf_mem_grow());
  /*if no more memory available return null*/
  if(hp==NULL){
    sf_errno=ENOMEM;
    return NULL;
              }
  else {
       /*move the epiloque  to the end of the heap*/
      sf_epilogue *epil=NULL;
      char * end=sf_mem_end();
      epil=(sf_epilogue *)(end-sizeof(sf_epilogue));
      epil->footer.info.allocated=1;
      epil->footer.info.prev_allocated=0;
      epil->footer.info.block_size=0>>4;
      epil->footer.info.requested_size=0;

      /*move the pointer from the epiloque to the payload*/
      blockSize=PAGE_SZ;
      hp=(sf_header*)((char*)hp-sizeof(sf_epilogue));
      hp->info.block_size=blockSize>>4;
      hp->info.allocated=0;
      hp->info.prev_allocated=0;

     /* set the footer */
     footer= (sf_footer*)(end-sizeof(sf_epilogue)-sizeof(sf_footer));
     footer->info.allocated=0;
     footer->info.block_size=blockSize>>4;
     footer->info.requested_size=0;
     footer->info.prev_allocated=0;


    /*coalesce the free block if needed*/
    hp=coalesceFreeBlock(hp);

       }

     }
  blockSize=(hp->info.block_size<<4);
  listPtr=find_position_for_newsize(blockSize);
  if((listPtr=sf_add_free_list(blockSize,listPtr))!=NULL){
      sf_add_free_node(&(listPtr->head),hp);                       }
  else  return NULL;


    }

     hp=sf_remove_free_node(&(listPtr->head));
     hp=place(hp,size);

}
/* return the pointer to the newly allocated area*/
    return (void*)hp+ sizeof(sf_header);
}


void sf_free(void *pp) {
    pp=(sf_header*)pp;
    sf_header *ptr=NULL;
    ptr=(sf_header*)pp;
    ptr=(sf_header*)((char*)ptr-sizeof(sf_header));
    /* 1.check if the pointer not null*/
    if(pp==NULL)
       abort();
    /* 2.check if the pointer is allocated block*/
    if(!ptr->info.allocated)
       abort();
    /* 3.The header of the block is before the end of the prologue*/
    sf_prologue* prologue=NULL;
    char* start=(char*)sf_mem_start();
    prologue=(sf_prologue*) (start+ sizeof(sf_prologue));
    if(((void*)prologue)>((void*)ptr))
       abort();

    /*3.1  The header of the block after the beginning of the epilogue.*/
    sf_epilogue *epilogue=NULL;
    char * end=sf_mem_end();
    epilogue=(sf_epilogue *)end-sizeof(sf_epilogue);
    if(((void*)ptr)>((void*)epilogue))
       abort();

    /*4 The block_size field is not a multiple of 16 or is less than the
      minimum block size of 32 bytes*/
      size_t newsize=ptr->info.block_size<<4;
      if(newsize<MIN_BLOCK_SIZE &&(newsize%16!=0))
        abort();

    /*5 The requested_size field, plus the size required for the block header,
      is greater than the block_size field.*/
     size_t request=0;
     request=ptr->info.requested_size;
     if((request+sizeof(sf_header))>newsize)
       abort();

     /* 6 If the prev_alloc field is 0, indicating that the previous block is free,
        then the alloc fields of the previous block header and footer should also be 0.*/
     if(!(ptr->info.prev_allocated)){
       sf_header * prevHdr=NULL;
       sf_footer * prevFtr=(sf_footer *) ((char*)ptr - sizeof(sf_footer));
       prevHdr=(sf_header*) ((char*)ptr-(prevFtr->info.block_size<<4));
       if((prevHdr->info.allocated))
         abort();
       if((prevFtr->info.allocated))
         abort();
       }



      /* set the header  info to free and create a footer for the free block*/
      sf_footer * footer=NULL;
      //size_t newsize=ptr->info.block_size<<4;
      ptr->info.allocated=0;
      ptr->info.requested_size=0;
      /*set the next blk prev_allocated field to 0*/
      sf_header *next=NULL;
      next=(sf_header*)((char*) ptr +newsize);
      next->info.prev_allocated=0;
      if(!next->info.allocated){
        sf_footer* nextFooter;
        nextFooter=(sf_footer*)(((char*)next+(next->info.block_size<<4))-sizeof(sf_footer));
        nextFooter->info.prev_allocated=0;
      }
      /*set the footer for new free block*/
      footer=(sf_footer*)(((char*)ptr+(ptr->info.block_size<<4))-sizeof(sf_footer));
      footer->info.allocated=0;
      footer->info.block_size=newsize>>4;
      footer->info.prev_allocated=ptr->info.prev_allocated;
      footer->info.two_zeroes=0;
      footer->info.requested_size=0;


      /* try to coalesce the block with adjustents blocks*/
      ptr=coalesceFreeBlock(ptr);
      /*search the list of the lists for the correct list size if exists*/
      newsize=ptr->info.block_size<<4;
      sf_free_list_node *list=NULL;
      if((list=find_free_list(newsize))!=NULL){
         sf_add_free_node(&(list->head),ptr);
                                                       }
      else{
          list=find_position_for_newsize(newsize);
          if((list=sf_add_free_list(newsize,list))!=NULL){
             sf_add_free_node(&(list->head),ptr);
                                                    }
          else sf_errno=ENOMEM;
                                       }

    return;
}



void *sf_realloc(void *pp, size_t rsize) {
  // pp=(sf_header*)pp;
    sf_header *hp=NULL;
    hp=(sf_header*)pp;
    hp=(sf_header*)((char*)hp-sizeof(sf_header));
    size_t origBlkSize=hp->info.block_size<<4;

    /*VALIDATING POINTER*/

    /* 1.check if the pointer not null*/
    if(pp==NULL)
      abort();
    /* 2.check if the pointer is allocated block*/
    if(!hp->info.allocated)
      abort();
    /* 3.The header of the block is before the end of the prologue*/
    sf_prologue* prologue=NULL;
    char* start=(char*)sf_mem_start();
    prologue=(sf_prologue*) (start+ sizeof(sf_prologue));
    if(((void*)prologue)>((void*)hp))
      abort();

    /*3.1  The header of the block after the beginning of the epilogue.*/
    sf_epilogue *epilogue=NULL;
    char * end=sf_mem_end();
    epilogue=(sf_epilogue *)end-sizeof(sf_epilogue);
    if(((void*)hp)>((void*)epilogue))
       abort();

    /*4 The block_size field is not a multiple of 16 or is less than the
      minimum block size of 32 bytes*/
      if(origBlkSize<MIN_BLOCK_SIZE &&(origBlkSize%16!=0))
        abort();

    /* 6 If the prev_alloc field is 0, indicating that the previous block is free,
        then the alloc fields of the previous block header and footer should also be 0.*/
     if(!(hp->info.prev_allocated)){
       sf_header * prevHdr=NULL;
       sf_footer * prevFtr=(sf_footer *) ((char*)hp - sizeof(sf_footer));
       prevHdr=(sf_header*) ((char*)hp-(prevFtr->info.block_size<<4));
       if((prevHdr->info.allocated))
         abort();
       if((prevFtr->info.allocated))
         abort();
       }



    /* return if the rsize is 0*/
    if(rsize==0)
      return NULL;


  /* END OF VALIDATION OF PARAMETERS*/

   /*REALLOCATING TO THE LARGER SIZE*/
   if(origBlkSize<rsize){
     sf_header *bigger_block=NULL;
   if((bigger_block=(sf_header*)sf_malloc(rsize))!=NULL){
     bigger_block=(sf_header*)memcpy(bigger_block,pp,(hp->info.requested_size));
     sf_free(pp);}
   else return NULL;
   return (void*)bigger_block;}

  /*REALLOCATING TO THE SMALLER SIZE BLOCK*/
   else  {
    /*try to split the block*/
    hp=place(hp,rsize);
    /*remove the new free block and try to coalesce*/
    
    sf_header *freeBlk=NULL;
    freeBlk= (sf_header*)(char*)hp+(hp->info.block_size<<4);
    sf_free_list_node * listHead=NULL;
    listHead=find_free_list(freeBlk->info.block_size<<4);
    sf_remove_free_node(&(listHead->head));
    
    /*coalesce block*/
    freeBlk=coalesceFreeBlock(freeBlk);
   
    /*add new coalesced block into proper list*/
    size_t newsize=freeBlk->info.block_size<<4;
    sf_free_list_node *list=NULL;
    
    if((list=find_free_list(newsize))!=NULL){
      sf_add_free_node(&(list->head),freeBlk);
                                                       }
    else{
        list=find_position_for_newsize(newsize);
        if((list=sf_add_free_list(newsize,list))!=NULL){
           sf_add_free_node(&(list->head),freeBlk);
                                                    }
        else sf_errno=ENOMEM;
                                       }

   }

   return hp+sizeof(sf_header);
}



sf_header *place(sf_header *hp,size_t size){
       size_t asize;
       sf_header *bp;
       
     /*adjust the size for the alignment requrements*/
       asize=size+ sizeof(sf_header);
       if(asize%16!=0)
         asize=ALIGN(asize);
     /* determine if the block have to be splitted*/
       size_t csize=hp->info.block_size << 4;
     /* pointer to the next block */
       sf_header *nextBlk=NULL;
     /*pointer to the footer of the new free block*/
       sf_footer *footer=NULL;
     /*pointer to the list */
       sf_free_list_node *list;
    /* the remaining size of the block */
       size_t newsize=csize-asize;

      if(newsize>=MIN_BLOCK_SIZE){
         /*split the block */
         hp->info.allocated=1;
         hp->info.requested_size=size;
         hp->info.block_size=(asize>>4);

         /* Set the remainder of the block as free */
         bp=(sf_header*)((char*)hp+asize);
         bp->info.allocated=0;
         bp->info.prev_allocated=1;
         bp->info.block_size=(newsize>>4);
      /* set the value of previous allocated to 0 for next block*/

      nextBlk=(sf_header*)(char*)bp+((bp->info.block_size << 4));
      nextBlk->info.prev_allocated=0;
      if(!nextBlk->info.allocated){
        sf_footer* nextF=NULL;
        nextF=(sf_footer*)(((char*)nextBlk+(nextBlk->info.block_size<<4))-sizeof(sf_footer));
        nextF->info.prev_allocated=0;
      }
        /*handle the footer of the new free block */
        footer=(sf_footer*)(((char*)bp+(bp->info.block_size<<4))-sizeof(sf_footer));
        footer->info.block_size=newsize>>4;
        footer->info.allocated=0;
        footer->info.prev_allocated=1;


       /*search the list of the lists for the correct list size if exists*/
       if((list=find_free_list(newsize))!=NULL){
          sf_add_free_node(&(list->head),bp);
                                                       }
      else{
          list=find_position_for_newsize(newsize);
          if((list=sf_add_free_list(newsize,list))!=NULL){
             sf_add_free_node(&(list->head),bp);
                                                    }
          else sf_errno=ENOMEM;
                                       }
          }

/*if no need to splitt*/
      else{
          hp->info.allocated=1;
          hp->info.requested_size=size;
          hp->info.block_size=(csize>>4);
      // nextBlk=(sf_header*) (char*)hp+((hp->info.block_size << 4));
      // nextBlk->info.prev_allocated=1;
      // if(!nextBlk->info.allocated){
      //   sf_footer* nextF=NULL;
      //   nextF=(sf_footer*)(((char*)nextBlk+(nextBlk->info.block_size<<4))-sizeof(sf_footer));
      //   nextF->info.prev_allocated=1;
      // }
          }
      return hp;

       }


/*
* Find Node before which to insert the new node.
* return  pointer to the node before the node
*/


  sf_free_list_node *find_position_for_newsize(size_t size){
    sf_free_list_node *current ;
    //if the list is empty insert before the head
    if((sf_free_list_head.next==&sf_free_list_head)&&(sf_free_list_head.prev==&sf_free_list_head))
       return  &sf_free_list_head;
    else {
         current = sf_free_list_head.next;
         while(current!= &sf_free_list_head && current->size < size){
              current = current->next;
             }
         if(current == &sf_free_list_head)
            return &sf_free_list_head;
          }
        return current;
                              }

/*
* Finds the proper list for the
* requested area of allocation
* return the pointer to the node
* that large enough for the memory request
*/

    sf_free_list_node *find_fit_for_size(size_t size) {
    sf_free_list_node *fnp = sf_free_list_head.next;
    while(fnp != &sf_free_list_head && fnp->size < size)
          fnp = fnp->next;
    if(fnp == &sf_free_list_head)
       return NULL;
    return fnp;

                                                      }
  /*
 * Finds the proper list to insert the newly freed block
 * return pointer to the list that have the size of free
 * block or null if the list of lists doesnt
 * contain the list of specified size
 * size_t size the size that of the list that need to be find
 */


    sf_free_list_node *find_free_list(size_t size) {
    sf_free_list_node *fnp = sf_free_list_head.next;
    while(fnp != &sf_free_list_head && fnp->size < size)
         fnp = fnp->next;
    if(fnp == &sf_free_list_head || fnp->size != size)
         return NULL;
    return fnp;
}



/*
 * Insert the new node into a list that consists of the headers
 * specified node in LIFO manner.
 * @param node  Node  which to insert
 */
    void sf_add_free_node(sf_header *head, sf_header *new){
        new->links.next = head->links.next;
        new->links.prev = head;
        new->links.next->links.prev = new;
        head->links.next =new;
 }

 /*
 * Remove the newly allocated node from a list that consists of the headers
 * specified node in LIFO manner.
 * @param node  Node  which to insert
 */

      sf_header *sf_remove_free_node(sf_header *head){
      sf_header *removedNode=head->links.next;
      head->links.next->links.next->links.prev=head;
      head->links.next=head->links.next->links.next;
      removedNode->links.next=NULL;
      removedNode->links.prev=NULL;

      return removedNode;
}


/*function coalesce the new free block with its neighbors*/

sf_header * coalesceFreeBlock(sf_header *oldBlock){
   sf_header * prevHdr=NULL;
   sf_free_list_node *list=NULL;
   sf_header * nextBlk=NULL;
   size_t s=oldBlock->info.block_size<<4;
   nextBlk=(sf_header*)((char*)oldBlock+s);
   sf_footer * nextFtr= (sf_footer*)((char*)nextBlk+((nextBlk->info.block_size<<4)-sizeof(sf_footer)));
   sf_footer * footer= (sf_footer*)((char*)oldBlock+((oldBlock->info.block_size<<4)-sizeof(sf_footer)));
   sf_footer * prevFtr=(sf_footer *) ((char*)oldBlock - sizeof(sf_footer));
   size_t size=oldBlock->info.block_size<<4;
   size_t prev_alloc=oldBlock->info.prev_allocated;
   size_t next_alloc=nextBlk->info.allocated;
   /* return unchaged pointer if prev and next blocks are allocated*/
   if(prev_alloc&&next_alloc)
      return oldBlock;
   /* coalesce if the previous block is free but next is allocated*/
   else if(!prev_alloc&&next_alloc){
     prevHdr=(sf_header*) ((char*)oldBlock-(prevFtr->info.block_size<<4));
     size_t prevBlk_size=prevFtr->info.block_size<<4;
    /* remove the previous block for the list*/
     if((list=find_free_list(prevBlk_size))!=NULL){
        sf_remove_free_node(&(list->head));
        size+=(prevBlk_size);
        footer->info.block_size=(size>>4);
        prevHdr->info.block_size=(size>>4);
        footer->info.prev_allocated=prevHdr->info.prev_allocated;
        oldBlock=prevHdr;
   }

                                       }

  /* coalesce if the next block is free but previous is allocated*/
  else if(prev_alloc&&!next_alloc){
    size_t nextBlk_size=nextBlk->info.block_size<<4;
    if((list=find_free_list(nextBlk_size))!=NULL){
        sf_remove_free_node(&(list->head));
       size+=nextBlk_size;
       oldBlock->info.block_size=(size>>4);
       nextFtr->info.block_size=(size>>4);
       nextFtr->info.prev_allocated=oldBlock->info.prev_allocated;}
                                                                      }
  else {
    size_t prevBlk_size=prevFtr->info.block_size<<4;
    list=find_free_list(prevBlk_size);
    sf_remove_free_node(&(list->head));
    size_t nextBlk_size=nextBlk->info.block_size<<4;
    list=find_free_list(nextBlk_size);
    sf_remove_free_node(&(list->head));
    size+=(prevBlk_size)+(nextBlk_size);
    prevHdr=(sf_header*) ((char*)oldBlock-(prevFtr->info.block_size<<4));
    prevHdr->info.block_size=(size>>4);
    nextFtr->info.block_size=(size>>4);
    nextFtr->info.prev_allocated=prevHdr->info.prev_allocated;
    oldBlock=prevHdr;
  }
   return oldBlock;
}
