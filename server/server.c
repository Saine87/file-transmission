#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include<sys/socket.h>
#include<sys/types.h>
#include<arpa/inet.h>

#include<sys/shm.h>
#include<sys/stat.h>
#include<sys/ipc.h>

#include <signal.h>

#define SUCCESS 0
#define FAILURE 1

#define MAXSTR   50          // dimensione massima per le stringhe varie
#define MAX      500         // dimensione fissata per il trasferimento UP

#define MAX_DOWN 1500        // dimensione massima che il server puo inviare al client in down


#define SECONDI     10       // tempo di bloccaggio nella recvfrom
#define N_TENTATIVI 5        // # tentativi di ristabilire la connessione

#define CHILDREN 4           // numero di figli possibili

#define PORTA 2500           // porta per il trasferimento



typedef struct comando{              //  comando ricevuto da tastiera
    char operazione[MAXSTR];         //  operazione da eseguire
    char nome_file[MAXSTR];          //  nome file del file
}t_comando;

typedef struct risp{                 // prima risposta fornita dal server
    int porta;                       // porta su cui verrà effettuato il trasferimento
    int concorrenza;                 // 0 non ci sono problemi di concorrenza , 1 altrimenti
}t_risp;

// pacchetti per scambiati durante UPLOAD

typedef struct pacchetto_up{         // pacchetto per trasferimento in up
    int ultimo;                      // 1 se ultimo pacchetto , 0 altrimenti
    int n_seq;                       // # frame
    char buf[MAX];                   // payload
}t_pacchetto_up;

typedef struct ack_up{               // pacchetto per trasferimento in up
    int n_seq;                       // ack
}t_ack_up;

// pacchetti scambiato durante DOWNLOAD

typedef struct pacchetto_down{       // pacchetto per trasferimento in down
    int ultimo;                      // 1 se ultimo pacchetto , 0 altrimenti
    int n_seq;                       // # frame
    char buf[MAX_DOWN];              // payload
}t_pacchetto_down;

typedef struct ack_down{             // pacchetto per trasferimento in down
    int n_seq;                       // frammento richiesto
    int size_frammento;              // size del frammento richiesto
}t_ack_down;


// gestione memoria condivisa

typedef struct shm{
    char cmd[MAXSTR];                // comando in corso d'esecuzione
    char nome_file[MAXSTR];          // nome del file
    pid_t pid;                       // pid del processo
    int free;                        // server sta eseguendo un trasferimento
}t_shm;



// variabili globali
typedef int *semaphore;
semaphore me;		         // Mutual Exclusion semaphore
int pid[4];                  // dove salvo i pid dei figli
t_shm *shared_memory;        // ptr all'area di memoria condivisa
int segment_id , id;


int crea_socket(int );                                                          // Creazione del socket
int menu(int , struct sockaddr_in );                                            // menu in cui ricevo il comando da eseguire
int up(int , t_comando *, socklen_t , struct sockaddr_in , int , t_shm *);      // trasferimento richiesto : UPLOAD
int down(int , t_comando *, socklen_t , struct sockaddr_in , int , t_shm *);    // trasferimento richiesto : DOWNLOAD
FILE *apriFile(char * , char *);                                                // apertura file più controllo errore
void stampa_tentativo(int );

// Funzioni per la gestione della sincronizzazione
semaphore make_sem();
void WAIT(semaphore s);
void SIGNAL(semaphore s);

// Funzioni per la gestione terminazione figli e processo padre
void sigint_handler ();
void child_handler();



int main( int argc , char *argv[] )
{
    int result;

    /*
        argv[0] : program name
        argv[1] : port
    */

    if( argc!=2 ){
        fprintf(stderr,  "Error : number of parameter\n");
        return(FAILURE);
    }else{

        result = crea_socket(atoi(argv[1]));
        if( result==FAILURE ){
            fprintf(stderr , "Error : Server not correct\n");
            return(FAILURE);
        }
        fprintf(stdout , "End Server \n");
    }

    return(SUCCESS);
}


int crea_socket(int port)
{
    // Dove viene ricevuto il comando
    int s , Bind;
    struct sockaddr_in saddr , caddr;
    socklen_t clilen;

    // Per il trasferimento del file
    int s1 , Bind1;
    struct sockaddr_in saddr1 , caddr1;
    socklen_t clilen1;
    int my_port = PORTA;

    t_comando cmd[1];
    t_risp risp[1];

    int result;

    s = socket(PF_INET , SOCK_DGRAM , IPPROTO_UDP);
    if( s < 0 ){
        fprintf(stderr , "Error : socket non creato\n");
        exit(1);
    }

    memset(&saddr , 0 , sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    Bind = bind(s , (struct sockaddr *) &saddr , sizeof(saddr));
    if( Bind < 0 ){
        fprintf(stderr , "Error : bind not correct\n");
        exit(1);
    }

    // gestione memoria condivisa
    int i , n;


    segment_id = shmget(IPC_PRIVATE , CHILDREN*sizeof(t_shm) , S_IRUSR | S_IWUSR);
    shared_memory = ( t_shm * )shmat(segment_id , NULL , 0); // il padre puo vedere la memoria condivisa

    for(i=0 ; i<CHILDREN ; i++){
        shared_memory[i].pid = 0;
    }

    id = 0;            // identificativo del padre
    me = make_sem();
    SIGNAL(me);        //  initialize to 1 sem me semaphore

    signal(SIGCHLD, child_handler);
    signal(SIGINT, sigint_handler);


    for(i=0 ; i<CHILDREN ; i++){
        n = fork();
        if( n < 0 ){
            fprintf(stderr , "Errore nella fork!!!");
            exit(1);
        }else if( n == 0 ){
            // figlio
            id = i;
            shared_memory = ( t_shm * )shmat(segment_id , NULL , 0);
            WAIT(me);
            shared_memory[i].pid = getpid();
            SIGNAL(me);

            while(1){

                my_port = PORTA;

                fprintf(stdout , "**** Server %d: attendo comandi dal Client ....\n" , id);

                clilen = sizeof(caddr);
                recvfrom(s , ( t_comando *)cmd , sizeof(cmd) , 0 , (struct sockaddr*) &caddr , &clilen);
                fprintf(stdout , "Server %d : Ho ricevuto un comando ... \n" , id);

                //fprintf(stdout , "Server %d : sto cercando di iniziare trasferimento su un'altra porta !\n" , id);
                int j;

                risp->concorrenza = 0;
                risp->porta = 0;

                // gestione concorrenza
                WAIT(me);
                for(j=0 ; j<CHILDREN ; j++){
                    if(j!=id){  // non sono io
                        if( shared_memory[j].free == 1 &&          // processo sta eseguendo un'operazione
                            strcmp(shared_memory[j].nome_file , cmd->nome_file) == 0 &&  // vogliamo eseguire un'oprazione sullo stesso file
                            !(strcmp(shared_memory[j].cmd , "DOWN") == 0 && strcmp(shared_memory[j].cmd , cmd->operazione) == 0)){ //  controllo che non sia l'unica operazione permessa DOWN DOWN
                                fprintf(stdout , "Problema concorrenza : Server %d sta eseguendo %s %s\n", id , shared_memory[j].cmd , shared_memory[j].nome_file);
                                risp->concorrenza = 1;
                            }
                    }
                }
                SIGNAL(me);



                // Invio un porta e comincio trasferimento del file su un'altra porta con il client
                if( risp->concorrenza == 1){
                    sendto(s , ( t_risp *)risp , sizeof(t_risp) , 0 , (struct sockaddr *) &caddr , clilen);  // invio che ho un problema di concorrezza
                }else{
                    // creo un socket su un'altra porta per il trasferimento del file


                    s1 = socket(PF_INET , SOCK_DGRAM , IPPROTO_UDP);
                    if( s1 < 0 ){
                        fprintf(stderr , "Error : socket non creato\n");
                        return(FAILURE);
                    }



                    memset(&saddr1 , 0 , sizeof(saddr1));
                    saddr1.sin_family = AF_INET;
                    saddr1.sin_addr.s_addr = htonl(INADDR_ANY);


                    do{
                        my_port++;
                        saddr1.sin_port = htons(my_port);
                        Bind1 = bind(s1 , (struct sockaddr *) &saddr1 , sizeof(saddr1));
                    }while(Bind1<0);

                    fprintf(stdout , "Server %d : Inzio trasferimento sulla porta : %d\n" , id , my_port);
                    risp->porta = my_port;

                    sendto(s , ( t_risp *)risp , sizeof(t_risp) , 0 , (struct sockaddr *) &caddr , clilen);


                    // invio la porta su cui voglio effettuare il trasferimento
                    // aspetto che il client mi risponda sulla porta specificata

                    clilen1 = sizeof(caddr1);
                    recvfrom(s1 , ( t_comando *)cmd , sizeof(cmd) , 0 , (struct sockaddr*) &caddr1 , &clilen1);   // da sbloccare


                    if(strcmp(cmd->operazione , "UP") == 0){
                        result = up(s1 , cmd , clilen1 , caddr1 , id , shared_memory);
                    }else if(strcmp(cmd->operazione , "DOWN") == 0){
                        result = down(s1 , cmd , clilen1, caddr1 , id , shared_memory);
                    }

                    close(s1);

                }

            }
            close(s);

        }else{
          // padre
           pid[i] = n;
        }
    }

    wait(NULL);

    return(SUCCESS);
}


int up(int s , t_comando *cmd , socklen_t clilen , struct sockaddr_in caddr , int id , t_shm *shared_memory)
{

    WAIT(me);
    shared_memory[id].free = 1;
    strcpy(shared_memory[id].cmd , cmd->operazione);
    strcpy(shared_memory[id].nome_file , cmd->nome_file);
    SIGNAL(me);


    fprintf(stdout , ">>Server %d :  Inzio UPLOAD file %s ... \n" , id , cmd->nome_file);

    FILE *fp;                                 // file da aprire
    t_pacchetto_up p[1];                      // pacchetto da inviare
    t_ack_up ack[1];                          // ack da ricevere

    fp = apriFile(cmd->nome_file , "UP");    // apertura del file

    if(fp==NULL) return(FAILURE);

    // inizializzazione pacchetti
    p->ultimo = 0;
    p->n_seq = 0;
    memset(&(p->buf) , 0 , sizeof(MAX));
    ack->n_seq = 1;

        // Inizio UPLOAD
    do{

        int tentativo , n , f_to , res;       // variabili varie per select + un flag

        fd_set fds;                           // file descriptor per la select per gestire mpx
        struct timeval tval;                  // gestione timeout
        FD_ZERO(&fds);                        // azzero l'insieme puntato da fds

        tentativo = 0;

        do{

            if(tentativo>0) stampa_tentativo(tentativo);

            f_to = FAILURE;                       // resetto il flag
            tval.tv_sec = SECONDI;                // secondi
            tval.tv_usec = 0;                     // microsecondi
            FD_SET(s , &fds);                     // aggiungo il socket a fds4

            n = select(FD_SETSIZE , &fds , NULL  , NULL , &tval);

            if( n == -1 ){
                fprintf(stderr , "Error in Select");
                exit(1);
            }else if( n > 0 ){

                clilen = sizeof(caddr);
                res =  recvfrom(s , ( t_pacchetto_up *) p , sizeof(p) , 0 , (struct sockaddr *) &caddr , &clilen);     // RECV
               // fprintf(stdout , "Ricevo frame %d ...\n" , p->n_seq);

                f_to = SUCCESS;

                if(ack->n_seq != p->n_seq){
                    // frammento ricevuto non corretto
                    f_to = FAILURE;

                }else{
                    if(p->ultimo==1) ack->n_seq = 0; // come ultimo ack invio 0
                  //  fprintf(stdout , "Invio ack %d ... \n" , ack->n_seq);
                    sendto(s , ( t_ack_up *) ack , sizeof(t_ack_up) , 0 , (struct sockaddr *) &caddr , clilen);      // SEND
                }
            }

            tentativo++;

        }while(f_to == FAILURE && tentativo < N_TENTATIVI);

        if(tentativo >= N_TENTATIVI){
            remove(cmd->nome_file);
            fprintf(stderr , "\nErrore : sequenza non corretta oppure Client in crash...\n\n");
            return(FAILURE);
        }

        if( p->ultimo == 0 ) fwrite(p->buf , sizeof(char) , MAX , fp);  // scrittura nel file

        ack->n_seq++;

    }while(p->ultimo == 0);


    fprintf(stdout , ">>Server %d :  Fine UPLOAD file %s\n" , id , cmd->nome_file);
    fclose(fp);

    WAIT(me);
    shared_memory[id].free = 0;
    SIGNAL(me);

    return(SUCCESS);
}


int down(int s , t_comando *cmd , socklen_t clilen , struct sockaddr_in caddr , int id , t_shm *shared_memory)
{
    WAIT(me);
    shared_memory[id].free = 1;
    strcpy(shared_memory[id].cmd , cmd->operazione);
    strcpy(shared_memory[id].nome_file , cmd->nome_file);
    SIGNAL(me);

    fprintf(stdout , ">>Server %d :  Inzio DOWNLOAD file %s\n" , id , cmd->nome_file);

    FILE *fp;
    int SIZE;

    fp = apriFile(cmd->nome_file , "DOWN");

    if(fp==NULL) return(FAILURE);

    // preparazione pacchetto da ricevere a ack da inviare
    t_pacchetto_down p[1];
    t_ack_down ack_d[1];
    int b_letti;
    int flag;
    int count;

    // inizializzazione pacchetti
    p->ultimo = 0;
    p->n_seq = 0;
    memset(&(p->buf) , 0 , sizeof(MAX_DOWN));
    ack_d->n_seq = 0;
    ack_d->size_frammento=-1;

    flag = 0;
    count = 1;

    do{


        int tentativo = 0;
        int f_to;
        int n;
        int res;
        fd_set fds;                           // file descriptor per la select per gestire mpx
        struct timeval tval;                  // gestione timeout
        FD_ZERO(&fds);

        do{

            if(tentativo>0) stampa_tentativo(tentativo);

            f_to = FAILURE;

            tval.tv_sec = SECONDI;                // secondi
            tval.tv_usec = 0;                     // microsecondi
            FD_SET(s , &fds);                     // aggiungo il socket a fds4

            n = select(FD_SETSIZE , &fds , NULL  , NULL , &tval);

            if( n == -1 ){
                fprintf(stderr , "Error in Select");
                exit(1);
            }else if( n > 0 ){

                clilen = sizeof(caddr);

                res =  recvfrom(s , ( t_ack_down * ) ack_d , sizeof(ack_d) , 0 ,(struct sockaddr *) &caddr , &clilen);
                //fprintf(stdout , "Ricevo ack %d ... \n" , ack_d->n_seq);

                if(ack_d->n_seq == -1){
                    // gestione abort del Client
                    fprintf(stdout , "Trasferimento annullato dal client\n");
                    fclose(fp);
                    return(0);
                }

                f_to = SUCCESS;

                if(res == 0){
                    printf("Connessione chiusa\n");
                    exit(1);
                }else if(res == -1){
                    printf("Recv Failed\n");
                    exit(1);
                }

                if(count != ack_d->n_seq){
                    // non mi aspetto questo count
                    f_to = FAILURE;
                }
            }

            tentativo++;

        }while(f_to == FAILURE && tentativo < N_TENTATIVI);

        if(tentativo >= N_TENTATIVI){
            fprintf(stderr , "\nErrore : sequenza non corretta oppure Client in crash...\n\n");
            return(FAILURE);
        }

        SIZE = ack_d->size_frammento;
        p->n_seq = ack_d->n_seq;
        b_letti = fread(p->buf , sizeof(char) , SIZE , fp);


        if(b_letti < SIZE){
            flag = 1;
            p->ultimo = 1;   // sto inviando l'ultimo pacchetto
        }
        //fprintf(stdout , "Invio frammento %d ... \n" , p->n_seq);
        sendto(s , ( t_pacchetto_down *) p , 2*sizeof(int)+SIZE , 0 , (struct sockaddr *) &caddr , clilen);
        count++;

    }while(flag==0);

    fclose(fp);
    WAIT(me);
    shared_memory[id].free = 0;
    SIGNAL(me);

    fprintf(stdout , ">>Server %d :  Fine DOWNLOAD file %s\n" , id , cmd->nome_file);

    return(SUCCESS);
}




void stampa_tentativo(int tentativo)
{
    fprintf(stderr , "\nTIMEOUT SCADUTO\n");
    fprintf(stderr , "Attenzione problema di connessione ...\n");
    fprintf(stderr , "tentativo di ristablire la connessione # (  %d \\ %d ) \n" , tentativo , N_TENTATIVI-1);
}

FILE *apriFile(char *nome_file , char *cmd){


    FILE *fp;

    if(strcmp(cmd , "UP")==0){
        if((fp=fopen(nome_file , "wb"))==NULL){
            fprintf(stderr , "Errore apertura del file.\n");
            return(NULL);
        }
    }else{
        if((fp=fopen(nome_file , "rb"))==NULL){
            fprintf(stderr , "Errore apertura del file.\n");
            return(NULL);
        }
    }
    return(fp);
}

semaphore make_sem()
{
    int *sem;

    sem = calloc(2,sizeof(int));
    pipe(sem);
    return sem;
}

void WAIT(semaphore s)
{
    int junk;

    if (read(s[0], &junk, 1) <=0) {
      fprintf(stderr, "ERROR : wait\n");
       exit(1);
       }
}

void SIGNAL(semaphore s)
{

    if (write(s[1], "s", 1) <=0) {
      fprintf(stderr, "ERROR : signal\n");
       exit(1);
    }
}


/**
 * child_handler handles signal SIGCHLD (child terminated)
 * the "while" handles the case when a child terminates during the execution of
 * the handler itself
 */
void child_handler() {

    // risetto shared_memory[x].free a 0 in caso di uccisione del server x
    // Rimuovo un file quando un processo viene ucciso durante un upload non completato
    WAIT(me);
    int x , y;
    for(x=0 ; x<CHILDREN ; x++){
        if(shared_memory[x].free == 1){
            y = kill(shared_memory[x].pid ,  0);
            if( y < 0 ){
                // si è verificato un errore
                if(strcmp(shared_memory[x].cmd , "UP")==0){
                    remove(shared_memory[x].nome_file);
                }
                shared_memory[x].free = 0;
            }
        }
    }
    SIGNAL(me);
	while(waitpid(-1, NULL, WNOHANG) > 0);
	signal(SIGCHLD, child_handler);
}

/**
 * sigint_handler handles signal SIGINT (CTRL-C)
 * send SIGTERM signal to each child
 */
void sigint_handler () {
	int i;


	for (i = 0; i < CHILDREN; i++)
		kill(pid[i], SIGTERM);

	while(waitpid(-1, NULL, WNOHANG) > 0);
	exit(0);
}



