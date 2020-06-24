#include <signal.h>  //To receive signals
#include <unistd.h>  //I need this to use intptr_t cause int and void* has not same size
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>  //To use threads
#include "./headers/Cashier_Functions.h"
#include "./headers/Config_Functions.h"
#include "./headers/Queue_Functions.h"
#include <errno.h>  //To manage errors

static long t; //TIME
static config * conf; //Program parameters organized in a struct
static FILE* logfile;

volatile sig_atomic_t sighup=0;
volatile sig_atomic_t sigquit=0;

static void handler (int nsig) {

if (nsig==1){
  sighup=1;
  printf("Signal Detected : SIGHUP\n"); fflush(stdout);
}
if (nsig==0){
  sigquit=1;
  printf("Signal Detected : SIGQUIT\n"); fflush(stdout);
}

printf("SuperMarket Closing\n"); fflush(stdout);
}

//Queues as abstraction of cashiers
static queue ** qs; //Array of queues to simulate the structure of a real market cashiers system
static pthread_mutex_t * q_mutex;
static pthread_cond_t * q_cond; //Condition variable on queues
static pthread_cond_t * wake_cond; //Used to wake up a cashier when there is some one in queue


//Bitmap (ARRAY) indicating whether the queue is closing or noy (0=Director is not closing the queue, 1=Director is closing the queue)
static int * closing_bit;
static pthread_mutex_t * closing_bit_mutex; //Mutex on the variable "closing_bit"

//After buying phase there will be an exitqueue where customer will wait CustomerDirector the ok to exit.
static queue * exitqueue;
static pthread_mutex_t exitqueque_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t okcond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t CustomerNeedsToExit = PTHREAD_COND_INITIALIZER;

//Used by clock to warn director when to open/close a cashier
static int * q_len; //Array that misures the lenght of every queue
static pthread_mutex_t * qlen_mutex;

//Bitmap (ARRAY) indicating whether the cashier has updated his status or not (lenght)
static int * q_update;
static pthread_mutex_t * update;
static pthread_mutex_t updated_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qlengh_updated_cond = PTHREAD_COND_INITIALIZER;


//To avoid conflicts writing (cashiers report)
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

static int exit_broadcast=0; //Warns sleeping cashiers when live_cus==0 (market is empty)


//////////////////////     UTILITY-FUNCTIONS      ////////////////////////////////////////

long gettime()
{
  struct timespec temp;
  clock_gettime(CLOCK_REALTIME, &temp);
  return ((temp.tv_sec)*1000+(temp.tv_nsec)/1000000);
}


void check_bitmap(int id,int* temp){
  pthread_mutex_lock(&closing_bit_mutex[id]);
  if (closing_bit[id]==1) *temp=1;
  pthread_mutex_unlock(&closing_bit_mutex[id]);
}

void update_qlen(int id){ //Updates the lengh of qs[id]
  pthread_mutex_lock(&qlen_mutex[id]);
  pthread_mutex_lock(&q_mutex[id]);
  q_len[id]=q_lenght(qs[id]);
  pthread_mutex_unlock(&q_mutex[id]);
  pthread_mutex_unlock(&qlen_mutex[id]);
}


int count_updated(){
  int temp=0;
  for (int i=0;i<conf->K;i++) {
      pthread_mutex_lock(&update[i]);
      if (q_update[i]==1) temp++;
      pthread_mutex_unlock(&update[i]);
  }
  return temp;
}

int count_opened(){
  int temp=0;
  for (int i=0;i<conf->K;i++) {
  pthread_mutex_lock(&q_mutex[i]);
  pthread_mutex_lock(&closing_bit_mutex[i]);
  if (qs[i]->queueopen==1 && closing_bit[i]!=1) temp++; //Checking how much queues are open
  pthread_mutex_unlock(&closing_bit_mutex[i]);
  pthread_mutex_unlock(&q_mutex[i]);
  }
  return temp;
}

/////////////////////////////////////////////////////////////////////////////////////////

void * d_foo(void *arg);
void * CashierMan_foo(void *arg);
void * cashier_foo(void *arg);
void * Cashier_clock(void *arg);
void * CustomerMan_foo (void *arg);
void * customer_foo (void *arg);


int main(int argc, char const *argv[])
{
    t=time(NULL);
    char* dir;
    sm_cashier * cashiers_array;
    struct sigaction signals;

    //LET'S CHECK CONFIG FILE TO SET THE PROGRAM
    if (argc!=2) {
        fprintf(stderr,"Main: You forgot the configfile path\n");
        exit(EXIT_FAILURE);
    }

    dir=malloc(50*sizeof(char));
    strcpy(dir,argv[1]);
    dir=strsep(&dir,"/");

    if (strcmp(dir,"test")==0){ //Secure test to check if the configfile comes from the safe directory
        if ((conf=test(argv[1]))==NULL){
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr,"Main : ConfigFile not found or its not the right one\n");
        exit(EXIT_FAILURE);
    }

    free(dir);

    //LET'S CHECK IF I'M ALLOWED TO OPEN THE LOGFILE TO WRITE EVENTS

    if ((logfile = fopen("logfile.log", "w")) == NULL) {
     fprintf(stderr,"Main : I don't have enough permissions (W) to open the logfile");
     exit(EXIT_FAILURE);
    }


    //Creating the "container" for cashier

    if ((cashiers_array = malloc(conf->K*sizeof(sm_cashier)))==NULL){
        fprintf(stderr, "Main : Trying to malloc more memory than your system can provide\n");
	    exit(EXIT_FAILURE);
    }
    for (int i=0;i<conf->K;i++){
      setupsm(&cashiers_array[i],i);
    }

    memset(&signals,0,sizeof(signals));
    signals.sa_handler=handler;
    if (sigaction(SIGHUP,&signals,NULL)==-1) {
        fprintf(stderr,"Main : Setting Signal-Catching function to manage SIGHUP error");
    }
    if (sigaction(SIGQUIT,&signals,NULL)==-1) {
        fprintf(stderr,"Main : Setting Signal-Catching function to manage SIGQUIT error");
    }

    //Create queues and mutex/condition vars to control them with threads.
    if ((qs=(queue**) malloc((conf->K)*sizeof(queue*)))==NULL || (q_mutex=malloc(conf->K*sizeof(pthread_mutex_t)))==NULL
         || (q_cond=malloc(conf->K*sizeof(pthread_cond_t)))==NULL || (wake_cond=malloc(conf->K*sizeof(pthread_cond_t)))==NULL) {

      fprintf(stderr, "Main : Trying to malloc more memory than your system can provide\n");
      exit(EXIT_FAILURE);
    }

    for (int i=0;i<conf->K; i++) {
      qs[i]=q_init();
      if (pthread_mutex_init(&q_mutex[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (q_mutex)\n");
        exit(EXIT_FAILURE);
      }
      if (pthread_cond_init(&q_cond[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (q_cond)\n");
        exit(EXIT_FAILURE);
      }
      if (pthread_cond_init(&wake_cond[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (wake_cond)\n");
        exit(EXIT_FAILURE);
      }
    }

    exitqueue=q_init();

    if((qlen_mutex=malloc(conf->K*sizeof(pthread_mutex_t)))==NULL || (q_len=malloc((conf->K)*sizeof(int)))==NULL){
      fprintf(stderr, "Main : Trying to malloc more memory than your system can provide\n");
      exit(EXIT_FAILURE);
    }

    for (int i=0;i<conf->K;i++){
      q_len[i]=q_lenght(qs[i]);
      if (pthread_mutex_init(&qlen_mutex[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (q_len)\n");
        exit(EXIT_FAILURE);
      }
    }


    if((closing_bit=malloc((conf->K)*sizeof(int)))==NULL || (closing_bit_mutex=malloc(conf->K*sizeof(pthread_mutex_t)))==NULL){
      fprintf(stderr, "Main : Trying to malloc more memory than your system can provide\n");
      exit(EXIT_FAILURE);
    }
    for (int i=0;i<conf->K;i++){
      closing_bit[i]=0;
      if (pthread_mutex_init(&closing_bit_mutex[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (closing_bit)\n");
        exit(EXIT_FAILURE);
      }
    }

    if ((q_update=malloc((conf->K)*sizeof(int)))==NULL || (update=malloc(conf->K*sizeof(pthread_mutex_t)))==NULL) {
      fprintf(stderr, "Main : Trying to malloc more memory than your system can provide\n");
      exit(EXIT_FAILURE);
    }
    for (int i=0;i<conf->K;i++){
      q_update[i]=0;
      if (pthread_mutex_init(&update[i], NULL) != 0) {
        fprintf(stderr, "Main : Pthread_Mutex_init Error (update array)\n");
        exit(EXIT_FAILURE);
      }
    }

    pthread_t director;

    if (pthread_create(&director,NULL,d_foo,(void*)cashiers_array)!=0) {
        fprintf(stderr,"Main : Insufficient resources to create another thread.");
            exit(EXIT_FAILURE);
    }

    if (pthread_join(director,NULL)!=0) {
            fprintf(stderr,"Main : No thread could be found corresponding to that specified by the given thread ID.(Director)");
        }

    //Every thread is closed,lets print results

    long t_custom=0, t_prod=0;
    float servicetime;
    fprintf(logfile, "\n\n");

    for (int i=0;i<conf->K;i++) {
        t_custom+=cashiers_array[i].ncustomers;
        t_prod+=cashiers_array[i].n_prod;
        servicetime = (cashiers_array[i].fixedtime * cashiers_array[i].ncustomers) + (cashiers_array[i].fixedtime*conf->S);
        fprintf(logfile, "CASHIER ID:%d => Products bought : %d | Customers served : %d | Time opened : %0.3f s | Average Service Time : %0.3f s | Times closed : %d |\n",cashiers_array[i].id, cashiers_array[i].n_prod, cashiers_array[i].ncustomers, (double) cashiers_array[i].time/1000,servicetime/1000,cashiers_array[i].nclosure);

    }
    fprintf(logfile, "\nTODAY'S STATS\n");
    fprintf(logfile, "CUSTOMERS SERVED: %ld\n",t_custom);
    fprintf(logfile, "PRODUCTS BOUGHT: %ld\n",t_prod);


    for (int i=0;i<conf->K; i++) free(qs[i]);

    free(qs); free(q_mutex); free(q_cond); free(wake_cond);
    free(qlen_mutex); free(q_len);
    free(exitqueue);
    free(closing_bit_mutex); free(closing_bit);
    free(q_update); free(update);
    free(conf);fclose(logfile); free(cashiers_array);
    return 0;
}

//DIRECTOR
void * d_foo(void *arg) {

  pthread_t CashierMan,CustomerMan;

  if (pthread_create(&CashierMan,NULL,CashierMan_foo, (void*) arg)) {
      fprintf(stderr,"Cashiers Manager: Insufficient resources to create another thread.");
      exit(EXIT_FAILURE);
  }

  if (pthread_create(&CustomerMan,NULL,CustomerMan_foo,NULL)) {
      fprintf(stderr,"Customers Manager: Insufficient resources to create another thread.");
      exit(EXIT_FAILURE);
  }

  if (pthread_join(CashierMan,NULL) == -1 ) {
          fprintf(stderr,"Director: CashierMan Thread Joining Error");
  }

  if (pthread_join(CustomerMan,NULL) == -1 ) {
          fprintf(stderr,"Director: CustomerMan Thread Joining Error");
  }

  return NULL;

}

//CASHIERS MANAGING

void * CashierMan_foo(void *arg) {
    pthread_t * cashiers;
    int open_cashiers=conf->s_cashiers;

    int to_remove; //Counting the number of queue that has at most 1 customer
    int overs2; //Checking if there is a queue with more then conf->S2 customers
    int found;
    int add_remove=0; //One cicle director controls queue to close, in the other director controls queue to open

    if ((cashiers=malloc(conf->K*sizeof(pthread_t)))==NULL){
        fprintf(stderr,"CashierMan_Foo: Trying to malloc more memory than your system can provide\n");
      exit(EXIT_FAILURE);
    }

    for(int i=0;i<open_cashiers;i++){//Starting the number of cashiers written in the configfile

          pthread_mutex_lock(&q_mutex[i]);
              qs[i]->queueopen=1;
          pthread_mutex_unlock(&q_mutex[i]);

          if (pthread_create(&cashiers[i],NULL,cashier_foo,&((sm_cashier*)arg)[i])!=0) {
              fprintf(stderr,"CashierMan_Foo: Cashier n.%d got an error in creation phase",i);
              exit(EXIT_FAILURE);
          }
      }

      while(sigquit!=1 && sighup!=1){
          to_remove=0;
          overs2=0;
          pthread_mutex_lock(&updated_mutex);
          while(to_remove<conf->S1 && overs2==0){

            if (sigquit!=1 && sighup!=1) {
              pthread_cond_wait(&qlengh_updated_cond,&updated_mutex); //Waiting the cashierclock to update qlengh and q_update

            if (count_opened()==count_updated()){//All cashiers have been updated, let's scan how many cashiers have to be close and how many have to be open

              for (int i=0;i<conf->K;i++){
              pthread_mutex_lock(&qlen_mutex[i]);
              pthread_mutex_lock(&q_mutex[i]);
              if(q_len[i]<=1 && qs[i]->queueopen==1) to_remove=1;
              if (q_len[i]>=conf->S2 && qs[i]->queueopen==1) overs2++;
              pthread_mutex_unlock(&q_mutex[i]);
              pthread_mutex_unlock(&qlen_mutex[i]);
                }
              }
            }
           else break;
          }
          pthread_mutex_unlock(&updated_mutex);

          for (int i=0;i<conf->K;i++) {
                  pthread_mutex_lock(&update[i]);
                  q_update[i]=0; //Reset the q_update variable after have checked them
                  pthread_mutex_unlock(&update[i]);
          }

          if (sigquit!=1 && sighup!=1){
            if (overs2>0 && add_remove%2==0) {
                found=-1;
                for (int i=0;i<conf->K;i++){
                    pthread_mutex_lock(&q_mutex[i]);
                    pthread_mutex_lock(&closing_bit_mutex[i]);
                    if(qs[i]->queueopen==0 && closing_bit[i]==0 && found==-1) found=i;
                    pthread_mutex_unlock(&closing_bit_mutex[i]);
                    pthread_mutex_unlock(&q_mutex[i]);
                }
                if (found!=-1) {//if I found a cashier that can be opened
                    pthread_mutex_lock(&q_mutex[found]);
                    qs[found]->queueopen=1; //Set queue open
                    pthread_mutex_unlock(&q_mutex[found]);
                    if (pthread_create(&cashiers[found],NULL,cashier_foo,&((sm_cashier*)arg)[found])!=0) { //Start cashier thread
                        fprintf(stderr,"CashierMan_Foo: Cashier n.%d got an error in creation phase",found);
                        exit(EXIT_FAILURE);
                    }
                }
            }
              if (to_remove==1 && add_remove%2==1 && count_opened()>1) {
                found=-1;
                for (int i=0;i<conf->K;i++){ //Find a cashier to close
                    pthread_mutex_lock(&q_mutex[i]);
                    pthread_mutex_lock(&qlen_mutex[i]);
                    if(q_len[i]<=1 && qs[i]->queueopen==1) found=i;
                    pthread_mutex_unlock(&qlen_mutex[i]);
                    pthread_mutex_unlock(&q_mutex[i]);
                }
                if (found!=-1) {//I found a cashier to close
                  pthread_mutex_lock(&closing_bit_mutex[found]);
                  closing_bit[found]=1; //Set the condition to close
                  pthread_mutex_unlock(&closing_bit_mutex[found]);
                  pthread_cond_signal(&q_cond[found]);//Wake customers in that queue to let him change the queue
                }
              }
              add_remove++;
          }
      }

      if (sigquit==1) {
          for (int i=0;i<conf->K;i++) { //To wake up cashiers that were waiting ,in case of a SIGQUIT
              pthread_mutex_lock(&q_mutex[i]);
              pthread_cond_signal(&wake_cond[i]);
              pthread_mutex_unlock(&q_mutex[i]);
          }
      }
      for (int i=0;i<conf->K;i++){
          if(((sm_cashier*)arg)[i].openedonce==1) {
              if (pthread_join(cashiers[i],NULL) == -1 ) {
                  fprintf(stderr,"CashierMan_Foo: Cashier n.%d got an error in joining phase",i);
              }
          }
      }
      free(cashiers);
      return NULL;
}

void * cashier_foo(void *arg) {
  long closing_time,opening_time,fixedtime,servicetime;
  int closing=0; //If the cashier has to close the cashier_foo
  unsigned int seed;
  opening_time=gettime();

  sm_cashier* cashier=((sm_cashier*)arg);
  cashier->openedonce=1;

  if (cashier->fixedtime==0) {
      while ((fixedtime = rand_r(&seed) % 80)<20); //Random number of time interval: 20-80
      cashier->fixedtime=fixedtime;
  }

  int id = cashier->id-1;
  seed=id+t; //Creating seed

  pthread_mutex_lock(&q_mutex[id]);
  qs[id]->queueopen=1;
  pthread_mutex_unlock(&q_mutex[id]);

  pthread_t clock;
  if (pthread_create(&clock,NULL,Cashier_clock,(void*) (intptr_t) (id))!=0) {
      fprintf(stderr,"Support clock thread of Cashier n.%d got an error in creation phase",id);
      exit(EXIT_FAILURE);
  }

  while(1){
      pthread_mutex_lock(&q_mutex[id]);
      while(closing==0 && q_lenght(qs[id])==0) { //Keep on cycling until the queue is to_remove or we have to close the cashier
          if (sigquit==1 || exit_broadcast==1) closing=1;
          check_bitmap(id,&closing);


          if (closing==0) {
              pthread_cond_wait(&wake_cond[id],&q_mutex[id]); //Idle cashier until a customer joins the queue
          }

          if (sigquit==1 || exit_broadcast==1) closing=1;
          check_bitmap(id,&closing);
      }

      if (closing==0)
      {
          customer * first=takefirstofqueue(&qs[id]); //Serves the customer that is the first in the queue
          pthread_mutex_unlock(&q_mutex[id]);

          servicetime=cashier->fixedtime+(conf->S*first->n_prod);
          struct timespec t={(servicetime/1000),((servicetime%1000)*1000000)};
          nanosleep(&t,NULL); //Sleep for servicetime msec

          //Customer paid
          first->paid=1;
          cashier->ncustomers++;
          cashier->n_prod+=first->n_prod;

          pthread_mutex_lock(&q_mutex[id]);
          pthread_cond_broadcast(&q_cond[id]); //Warn the client paid and reactivate customer thread
          check_bitmap(id,&closing); //Checks if bitmap has been modified and we have to close
      }
      if(closing==1 || sigquit==1 ) {
          closing_time=gettime();
          qs[id]->queueopen=0;
          cashier->time+=closing_time-opening_time; //calculating time and closing queueopen
          cashier->nclosure++;

          pthread_mutex_unlock(&q_mutex[id]);
          if (pthread_join(clock,NULL) == -1 ) {
              fprintf(stderr,"Support clock thread of Cashier n.%d got an error in joining phase",id);
          }

          pthread_mutex_lock(&closing_bit_mutex[id]);   //Changing status of cashier in the closing bitmap
          closing_bit[id]=0;
          pthread_mutex_unlock(&closing_bit_mutex[id]);


          pthread_mutex_lock(&q_mutex[id]);
          pthread_cond_broadcast(&q_cond[id]); //The cashier is closing , i need to warn all customer waiting to pay to change cashier
          empties_q(&qs[id]);
          pthread_mutex_unlock(&q_mutex[id]);

          return NULL;
      }
      pthread_mutex_unlock(&q_mutex[id]);
  }
}

void * Cashier_clock(void *arg) {
  int id=(int) (intptr_t) arg;
  int closing;
  while(1){

      check_bitmap(id,&closing);

      if (closing==0) {
          struct timespec t={(conf->t_update/1000),((conf->t_update%1000)*1000000)};
          nanosleep(&t,NULL);
      }

      check_bitmap(id,&closing);

      if (closing==0 && sigquit==0 && sighup==0 ) {

          update_qlen(id);

          pthread_mutex_lock(&update[id]);
          q_update[id]=1; //To warn the director that the cashier "id" has updated the queue's length info
          pthread_mutex_unlock(&update[id]);

          check_bitmap(id,&closing);

          pthread_mutex_lock(&updated_mutex);
          if (closing==0) pthread_cond_signal(&qlengh_updated_cond); //Wake up the CashierMan when has been updated
          pthread_mutex_unlock(&updated_mutex);
      }
      else {
          if (sigquit==1 || sighup==1) {
              pthread_mutex_lock(&updated_mutex);
              pthread_cond_signal(&qlengh_updated_cond); //Wake up the CashierMan to close the cashier
              pthread_mutex_unlock(&updated_mutex);
          }
          pthread_exit(NULL);
      }
  }
}



//CUSTOMERS MANAGING

void * CustomerMan_foo (void *arg){

      pthread_t * cs_threads;
      int start_cus,live_cus;  //start_cus = conf->C (customers that enter in the entry phase) ; live_cus= customers in the market at the moment when we read the var
      int i,size;

      start_cus=conf->C;
      live_cus=start_cus;
      size=live_cus;

      //Allocating and starting the array of threads to simulate starting entry customers
      if ((cs_threads=malloc(start_cus*sizeof(pthread_t)))==NULL){
          fprintf(stderr, "CustomerMan : Trying to malloc more memory than your system can provide\n");
  	    exit(EXIT_FAILURE);
      }

      for (i=0;i<start_cus;i++) {
          if (pthread_create(&cs_threads[i],NULL,customer_foo,(void*) (intptr_t) i)!=0) {
              fprintf(stderr,"CustomerMan : Customer %d Thread Creation Error",i);
              exit(EXIT_FAILURE);
          }
      }

      while(live_cus!=0){ //Until someone is in the market
          pthread_mutex_lock(&exitqueque_mutex);
          while (live_cus!=0) {
             if(q_lenght(exitqueue)==0) pthread_cond_wait(&CustomerNeedsToExit,&exitqueque_mutex); //Wake up the director when there is at least one customer in the exitqueque
             customer * first = takefirstofqueue(&exitqueue);
             first->allowedtoexit=1;
             live_cus--;
             pthread_cond_signal(&okcond); //Director allowed the customer to exit , wakes up customer thread and close it before continue
             if (live_cus==0) {
               pthread_mutex_lock(&q_mutex[i]);
               exit_broadcast=1;
                 for (int i=0;i<conf->K;i++) {   //If the customer was the last one , set exit_broadcast=1 and wake up cashiers to let them close
                      pthread_cond_signal(&wake_cond[i]);
                 }
                 pthread_mutex_unlock(&q_mutex[i]);
             }
             if(sighup!=1 && sigquit!=1 && live_cus==(conf->C-conf->E)) break;
          }
          pthread_mutex_unlock(&exitqueque_mutex);

          if (sighup!=1 && sigquit!=1 && live_cus==(conf->C-conf->E)) {
              size+=conf->E;
              if((cs_threads=realloc(cs_threads,size*sizeof(pthread_t)))==NULL) { // Adding E customers
                  fprintf(stderr,"CustomerMan :Realloc Error");
                  exit(EXIT_FAILURE);
              }
              for(i=size-conf->E;i<size;i++){//Starting last added customers
                  if (pthread_create(&cs_threads[i],NULL,customer_foo,(void*) (intptr_t) i)!=0) {
                      fprintf(stderr,"CustomerMan : Customer %d Thread Creation Error",i);
                      exit(EXIT_FAILURE);
                  }
                  start_cus++;
                  live_cus++;
               }
          }
      }

      for (i=0;i<size;i++){
          if (pthread_join(cs_threads[i],NULL) == -1 ) {
              fprintf(stderr,"Director: Customer Thread n.%d Join error",i);
          }
      }//I'll close the thread only when all customers finished
      free(cs_threads);
      return NULL;
}


void * customer_foo (void *arg) {
  long entry_time,exit_time,q_entry=-1,q_exit=-1;
  entry_time=gettime();

  long fixedtime; //Random time to buy products
  int q_choice;
  int switching;
  int q_valid;

  customer* custom = malloc(sizeof(customer));
  init_customer(custom,(int) (intptr_t) (arg));
  unsigned int seed=custom->id-1+t; //Creating seed

  custom->n_prod=rand_r(&seed)%(conf->P);
  while ((fixedtime = rand_r(&seed) % (conf->T))<10); //FixedTime to buy

  struct timespec t={(fixedtime/1000),((fixedtime%1000)*1000000)};
  nanosleep(&t,NULL); //Sleeping to simulate buying phase

  if ((custom->n_prod)!=0){
      do{
          q_valid=0;
          switching=0; //Reset change queue value
          do {
              q_choice=rand_r(&seed) % (conf->K); //Random queue number
              pthread_mutex_lock(&q_mutex[q_choice]);
              if (qs[q_choice]->queueopen!=0) q_valid=1; //Check if the queue is open
              pthread_mutex_unlock(&q_mutex[q_choice]);
          }while(q_valid==0 || sigquit!=0);
          if (sigquit!=1){
            pthread_mutex_lock(&q_mutex[q_choice]);
            custom->queuechecked++;

            if(custom->queuechecked==1) q_entry=gettime(); //It's his first cashier

            if(append(&qs[q_choice],&custom)==-1){ //Joining the queue chosen
                fprintf(stderr, "Fail joining cashier-queue\n");
                exit(EXIT_FAILURE);
              }
              pthread_cond_signal(&wake_cond[q_choice]); //Signal wake up cashier if she is waiting for clients

           //wait until the customer has paid and if the manager closes the cashier, move it to another cashier
              while ((custom->paid)==0 && switching==0){
                  pthread_cond_wait(&q_cond[q_choice],&q_mutex[q_choice]);
                  if (qs[q_choice]->queueopen==0) switching=1;
              }
              pthread_mutex_unlock(&q_mutex[q_choice]);
          }
          else break;
      }while(custom->paid==0);
      if (q_entry !=-1) q_exit=gettime();

  }
  if(q_entry!=-1) q_exit=gettime();
  //The customer reachs the exitqueue...
  pthread_mutex_lock(&exitqueque_mutex);
  if(append(&exitqueue,&custom)==0){
      fprintf(stderr,"Fail joining exit-queue\n");
  }
  //and waits until CustomerMan allows him to exit
  pthread_cond_signal(&CustomerNeedsToExit);
  while(custom->allowedtoexit!=1) { pthread_cond_wait(&okcond,&exitqueque_mutex);}
  pthread_mutex_unlock(&exitqueque_mutex);

  exit_time=gettime();
  custom->timeq=q_exit-q_entry;
  custom->time=exit_time-entry_time;

  if (custom->paid!=1) custom->n_prod=0; //A customer that did not pay, did not buy nothing

  //Write on the logfile the sumup
  pthread_mutex_lock(&write_mutex);
  fprintf(logfile,"CUSTOMER ID:%d => Products bought:%d | Time in the supermarket: %0.3f s | Queue Time: %0.3f s | n. Queue visited: %d | \n",custom->id,custom->n_prod, (double) custom->time/1000, (double) custom->timeq/1000, custom->queuechecked);
  pthread_mutex_unlock(&write_mutex);

  free(custom);
  return NULL;
}
