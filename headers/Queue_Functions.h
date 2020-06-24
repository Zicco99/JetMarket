
typedef struct sm_custom{
    int id;
    int n_prod;
    int time;
    int timeq;
    int queuechecked;
    int paid;
    int allowedtoexit;
}customer;


void init_customer(customer * custom, int i) {
    custom->id = (i+1);
    custom->n_prod=0;
    custom->time=0;
    custom->timeq=0;
    custom->queuechecked=0;
    custom->paid=0;
    custom->allowedtoexit=0;
}


typedef struct queue {
    struct queuenode *head;
    int queueopen;
}queue;

typedef struct queuenode{
    customer *custom;
    struct queuenode *next;
}queuenode;


int append(queue ** q,customer ** custom) {
  queuenode * new;
    if((new=malloc(sizeof(queuenode)))==NULL){
        return -1;
    }
    new->custom=(*custom);
    new->next=NULL;
    queuenode * curr = (*q)->head;
    if ((*q)->head==NULL){
        (*q)->head=new;
        return 1;
    }
    while(curr->next!=NULL) curr=curr->next;
    curr->next = new;
    return 1;
}

queue * q_init(){
       struct queue* temp=malloc(sizeof(queue));
       temp->head=NULL;
       temp->queueopen=0;
       return temp;
 }

 void empties_q(queue **q) {
    while((*q)->head!=NULL) {
        queuenode * curr = (*q)->head;
        (*q)->head=((*q)->head)->next;
        free(curr);
    }
  }

  int q_lenght(queue *q) {
      queuenode *curr=q->head;
      int n=0;
      while(curr!=NULL){
          n++;
          curr=curr->next;
      }
      return n;
  }

 customer * takefirstofqueue(queue **q) {
   queuenode * first = (*q)->head;
   (*q)->head=((*q)->head)->next;
   customer * tmp = first->custom;
   free(first);
   return tmp;
 }
