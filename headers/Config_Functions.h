
#define MAX_LEN 256

typedef struct config {
    int K; //Max Cashiers 
    int C; //Max Customers
    int E; //After E customers exit, let others enter
    int T; //Min BuyTime
    int P; //Max Products for customer
    int S; //Product ScanTime , different for each customer
    int S1; //If there are S1 queues with max 1 customer then close one of them
    int S2; //If there are S2 queues with more than s2 customer then open one cashier
    int s_cashiers; //How many cashier are opened at the start
    int t_update; //Time passed till the next update of cashiersIf there are S1 queues with max 1 customer then close one cashierIf there are S1 queues with max 1 customer then close one cashier queue lenght to CashierMan
}config;


int confcheck(config * conf) {
    int rightconf=1;
    if(!(conf->K>0)) {fprintf(stderr,"K must be > 0 \n"); rightconf=0;}
    if(!(conf->C>1)) {fprintf(stderr,"C must be > 1 \n"); rightconf=0;}
    if(!(conf->E>0 && conf->E<conf->C)) {fprintf(stderr,"E must be 0 < E < C \n"); rightconf=0;}
    if(!(conf->T>10)) {fprintf(stderr,"T must be > 10 \n"); rightconf=0;}
    if(!(conf->P>=0)) {fprintf(stderr,"P must be >= 0 \n"); rightconf=0;}
    if(!(conf->S>0)) {fprintf(stderr,"S must be > 0 \n"); rightconf=0;}
    if(!(conf->S1>0)) {fprintf(stderr,"S1 must be > 0 \n"); rightconf=0;}
    if(!(conf->S2>0 && conf->S1<conf->S2)) {fprintf(stderr,"S2 must be 0 < S2 < S1 \n"); rightconf=0;}
    if(!(conf->s_cashiers>0 &&  conf->s_cashiers<=conf->K)) {fprintf(stderr,"s_cashiers must be 0<s_cashiers<K \n"); rightconf=0;}
    if(!(conf->t_update)) {fprintf(stderr,"t_update must be > T \n"); rightconf=0;}
    if(rightconf==0) printf(" Solve problems above about parameters \n");
    return rightconf;
}

void printconf(config configvalues) {
    printf("%d %d %d %d %d %d %d %d %d %d\n", configvalues.K, configvalues.C, configvalues.E, configvalues.T, configvalues.P, configvalues.S, configvalues.S1, configvalues.S2, configvalues.s_cashiers, configvalues.t_update);
}

config * test(const char* configfile) {
    config * conf;
    FILE *fd=NULL;
    char *buffer,*n_param,*line;


    if ((fd=fopen(configfile, "r")) == NULL) {
      fprintf(stderr,"I'm not allowed to read the file or file doesn't exits");
        fclose(fd);
	    return NULL;
    }

    if ((conf=malloc(sizeof(config))) == NULL || (buffer=malloc(MAX_LEN*sizeof(char))) == NULL) {
      fprintf(stderr,"Trying to malloc more memory than your system can provide\n");
        fclose(fd); free(conf); free(buffer);
	    return NULL;
    }

    while(fgets(buffer,MAX_LEN,fd) != NULL) {
        line=buffer;
        n_param = strsep(&line,"=");                //First strsep extracts the name , second strsep extracts the value
        if(strcmp(n_param,"K")==0) conf->K=atoi(strsep(&line,"="));
        if(strcmp(n_param,"C")==0) conf->C=atoi(strsep(&line,"="));
        if(strcmp(n_param,"E")==0) conf->E=atoi(strsep(&line,"="));
        if(strcmp(n_param,"T")==0) conf->T=atoi(strsep(&line,"="));
        if(strcmp(n_param,"P")==0) conf->P=atoi(strsep(&line,"="));
        if(strcmp(n_param,"S")==0) conf->S=atoi(strsep(&line,"="));
        if(strcmp(n_param,"S1")==0) conf->S1=atoi(strsep(&line,"="));
        if(strcmp(n_param,"S2")==0) conf->S2=atoi(strsep(&line,"="));
        if(strcmp(n_param,"s_cashiers")==0) conf->s_cashiers=atoi(strsep(&line,"="));
        if(strcmp(n_param,"t_update")==0) conf->t_update=atoi(strsep(&line,"="));
   }
   
   ///printconf(*conf);

    if (confcheck(conf)==0) {
        fclose(fd);
        free(buffer);
        free(conf);
        return NULL;
    }
    free(buffer);
    fclose(fd);
    return conf;
}



