typedef struct sm_cashier {
    int id;
    int n_prod;
    int ncustomers;
    int time;
    float servicetime;
    int nclosure;
    long fixedtime;
    int openedonce;
}sm_cashier;

void setupsm(sm_cashier * sm, int i) {
    sm->id = (i+1);
    sm->n_prod=0;
    sm->ncustomers=0;
    sm->time=0;
    sm->servicetime=0;
    sm->nclosure=0;
    sm->fixedtime=0;
    sm->openedonce=0;
}
