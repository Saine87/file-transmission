#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include<sys/socket.h>
#include<sys/types.h>
#include<arpa/inet.h>

#define SUCCESS 0
#define FAILURE 1

#define MAXSTR   50          // dimensione massima per le stringhe varie
#define MAX      500         // dimensione fissata per il trasferimento UP

#define MAX_DOWN 1500        // dimensione massima che il server puo inviare al client in down
#define SIZE_DOWN 500        // size del frammento che viene chiesta dal client ( < 1500 )


#define SECONDI     10       // tempo di bloccaggio nella recvfrom
#define N_TENTATIVI 5        // # tentativi di ristabilire la connessione



typedef struct comando{                  //  comando ricevuto da tastiera
    char operazione[MAXSTR];             //  operazione da eseguire
    char nome_file[MAXSTR];              //  nome file del file
}t_comando;

typedef struct coda{                     // Coda dei comandi da eseguire
  t_comando cmd;                         // comando da eseguire
  struct coda *next;                     // ptr all'elemento successivo
}t_coda;

typedef struct risp{                     // prima risposta fornita dal server
    int porta;                           // porta su cui verrà effettuato il trasferimento
    int concorrenza;                     // 0 non ci sono problemi di concorrenza , 1 altrimenti
}t_risp;

// pacchetti per scambiati durante UPLOAD
typedef struct pacchetto_up{               // pacchetto per trasferimento in up
    int ultimo;                            // 1 se ultimo pacchetto , 0 altrimenti
    int n_seq;                             // # frame
    char buf[MAX];                         // payload
}t_pacchetto_up;

typedef struct ack_up{                     // pacchetto per trasferimento in up
    int n_seq;                             // ack
}t_ack_up;

// pacchetti scambiato durante DOWNLOAD

typedef struct pacchetto_down{             // pacchetto per trasferimento in down
    int ultimo;                            // 1 se ultimo pacchetto , 0 altrimenti
    int n_seq;                             // # frame
    char buf[MAX_DOWN];                    // payload
}t_pacchetto_down;

typedef struct ack_down{                   // pacchetto per trasferimento in down
    int n_seq;                             // frammento richiesto
    int size_frammento;                    // size del frammento richiesto
}t_ack_down;


// prototipi
int crea_socket(char * , char *);                                    // creazione del socket ...
int menu(int , struct sockaddr_in , char *);                         // menu iniziale
t_coda *up(int , struct sockaddr_in  , char * , t_coda *);           // funzione gestione upload
t_coda *down(int , struct sockaddr_in , char * , t_coda *);          // funzione gestione download
FILE *apriFile(char * , char *);                                     // apertura file più controllo errore
void stampa_tentativo(int );                                         // stampo messaggio sullo schermo

// Funzioni per la gestione della coda
t_coda* newE ();
t_coda *enqueue (t_coda *, t_comando *);
t_coda *dequeue (t_coda *, char * , char*);
void traversal(t_coda *);



int main( int argc , char *argv[] )
{
    int result;
    /*
        argv[0] : program name
        argv[1] : address
        argv[2] : port
    */

    if( argc!=3 ){
        fprintf(stderr , "Error : Number parameter\n");
        return(FAILURE);
    }else{
        result = crea_socket(argv[1] , argv[2]);
        if(result==FAILURE){
            fprintf(stderr , "Error : Program not correct\n");
            return(FAILURE);
        }
        fprintf(stdout , "End client\n");
    }

    return(SUCCESS);
}


int crea_socket(char *address , char *tport)
{
    struct sockaddr_in saddr;
    struct in_addr addr;
    int s;

    inet_aton(address , &addr);
    s = socket(PF_INET , SOCK_DGRAM , IPPROTO_UDP);
    if( s < 0 ){
        fprintf(stderr , "Error : socket not created\n");
        return(FAILURE);
    }

    memset(&saddr , 0 , sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(atoi(tport));
    saddr.sin_addr.s_addr = addr.s_addr;

    menu(s , saddr , address);
    return(SUCCESS);
}


int menu(int s , struct sockaddr_in saddr , char *address)
{

    t_comando cmd[1];              // comando
    t_coda *pTail = NULL;          // coda dei comandi
    t_risp risp[1];                // prima risposta del server

    struct sockaddr_in saddr1;
    struct in_addr addr1;
    int s1;

    //  variabili per la select
    fd_set fds;                    // file descriptor per la select per gestire mpx
    int n;                         // return della select
    struct timeval tval;           // gestione timeout
    tval.tv_sec = 0;               // secondi
    tval.tv_usec = 0;              // microsecondi
    FD_ZERO(&fds);                 // azzero l'insieme puntato da fds
    FD_SET(s , &fds);              // aggiungo il socket a fds1
    FD_SET(fileno(stdin) , &fds);  // aggiungo lo stdin a fds1

    int flag = 0;

    while(1){

        if(( n = select(FD_SETSIZE , &fds , &fds , NULL , &tval)) == -1 ){
            fprintf(stderr , "Error in Select\n");
            return(FAILURE);
        }

        if(FD_ISSET(s , &fds) == 1){

            FD_SET(fileno(stdin) , &fds);

            if ( pTail != NULL ){
                // Ho un comando in coda
                pTail = dequeue(pTail , cmd->operazione , cmd->nome_file);
                sendto(s , ( t_comando *)cmd , sizeof(cmd) , 0 , (struct sockaddr *) &saddr , sizeof(saddr));  // invio comando al server

                // Per la select : sblocco la recvfrom
                fd_set fds1;
                struct timeval tval1;
                int n1;

                FD_ZERO(&fds1);
                FD_SET(s , &fds1);

                int tentativo;        // Tentativo per aspettare la recvfrom
                int flag1=FAILURE;    // flag1 = SUCCESS se recvfrom è OK

                tentativo=0;
                flag1=FAILURE;

                do{

                    if(tentativo>0) stampa_tentativo(tentativo);

                    tval1.tv_sec = SECONDI;
                    tval1.tv_usec = 0;

                    n1 = select(FD_SETSIZE , &fds1 , NULL , NULL , &tval1);

                    if( n1 == -1 ){
                        fprintf(stderr , "Error in Select");
                        return(FAILURE);
                    }

                    if( n1 > 0 ){
                        recvfrom(s , ( t_risp *) risp , sizeof(t_risp) , 0 , 0 , 0 );    //  RECV
                        flag1 = SUCCESS;
                    }

                    tentativo++;

                }while(flag1 == FAILURE && tentativo < N_TENTATIVI);

                if(tentativo>=N_TENTATIVI){
                    fprintf(stderr , "Server non connesso...\n");
                    close(s);
                    exit(1);
                }



                if(risp->concorrenza == 0){

                    // creo un altro socket sulla porta specificata dal server
                    s1 = socket(PF_INET , SOCK_DGRAM , IPPROTO_UDP);
                    if( s1 < 0 ){
                        fprintf(stderr , "Error : socket not created\n");
                        return(FAILURE);
                    }

                    fprintf(stdout , "Sto per aprire un socket indirizzo %s , porta %d\n" , address , risp->porta);

                    inet_aton(address , &addr1);
                    memset(&saddr1 , 0 , sizeof(saddr1));
                    saddr1.sin_family = AF_INET;
                    saddr1.sin_port = htons(risp->porta);
                    saddr1.sin_addr.s_addr = addr1.s_addr;

                    sendto(s1 , ( t_comando *)cmd , sizeof(cmd) , 0 , (struct sockaddr *) &saddr1 , sizeof(saddr1)); // reinvio il comando voluto sulla nuova porta ( overhead )

                    if(strcmp(cmd->operazione , "UP") == 0) pTail = up(s1 , saddr1 , cmd->nome_file , pTail);
                    if(strcmp(cmd->operazione , "DOWN") == 0) pTail = down(s1 , saddr1 , cmd->nome_file , pTail);

                    close(s1);
                    // chiudo il socket che ho usato per il trasferimento del file

                }else{
                    // Errore problemi di concorrenza
                    fprintf(stdout , "Attenzione : problema di accesso concorrente allo stesso File\n");
                }
            }
        }


        if(pTail == NULL && n == 2 && FD_ISSET(fileno(stdin) , &fds) == 1){

            FD_SET(s , &fds);

            fprintf(stdout , "Inserisci cmd ( UP <nome_file> , DOWN <nome_file> , fine ) : \n");
            fscanf(stdin , "%s" , cmd->operazione);

            if(strcmp(cmd->operazione , "UP")==0){
                fscanf(stdin , "%s" , cmd->nome_file);
                FILE *fp1;
                fp1 = apriFile(cmd->nome_file , "UP");
                if(fp1!=NULL){
                    pTail = enqueue(pTail , cmd);
                    fclose(fp1);
                }

            }else{
                if(strcmp(cmd->operazione , "DOWN")==0){
                    fscanf(stdin , "%s" , cmd->nome_file);
                    pTail = enqueue(pTail , cmd);
                }else{
                    if(strcmp(cmd->operazione , "fine")!=0){
                        fprintf(stderr , "Errore : comando non disponibile!\n");
                    }else{
                        return(SUCCESS);
                    }
                }
            }
        }
    } // fine while

    return(SUCCESS);
}


t_coda *down(int s , struct sockaddr_in saddr , char *file , t_coda *pTail)
{

    // Apertura del file
    FILE *fp;
    fp = apriFile(file , "DOWN");    // apertura del file
    if(fp==NULL) return(pTail);      // errore apertura del file


    fprintf(stdout , "Inzio download file : %s ... \n" , file);

    // pacchetti da inviare
    t_pacchetto_down p[1];
    t_ack_down ack_d[1];


    // inizializzo pacchetti da inviare
    p->ultimo = 0;
    p->n_seq = 0;
    memset(&(p->buf) , 0 , sizeof(MAX_DOWN));
    ack_d->n_seq = 0;
    ack_d->size_frammento = SIZE_DOWN;

    t_comando cmd[1];

    int flag = 0;
    int count = 1;
    int abort = 0;

    // Gestione Select
    fd_set fds1 , fds2;                    // file descriptor per la select per gestire mpx
    int n;                                 // return della select
    struct timeval tval;                   // gestione timeout
    tval.tv_sec = 0;                       // secondi
    tval.tv_usec = 0;                      // microsecondi
    FD_ZERO(&fds1);                        // azzero l'insieme puntato da fsd2 ( stdin )
    FD_ZERO(&fds2);                        // azzero l'insieme puntato da fds3 ( socket )
    FD_SET(fileno(stdin) , &fds1);         // aggiungo lo stdin a fds1
    FD_SET(s , &fds2);                     // aggiungo il socket a fds1

    int tentativo = 0;
    int f_to;

    do{

        if(( n = select(s+1 , &fds1 , &fds2 , NULL , &tval)) == -1 ){
            fprintf(stderr , "Errore in Select\n");
            exit(1);
        }

        if(FD_ISSET(s , &fds2) == 1){

            FD_SET(fileno(stdin) , &fds1);

            fd_set fds3;                           // file descriptor per la select per gestire mpx
            int n3;                                // return della select
            struct timeval tval3;                  // gestione timeout
            FD_ZERO(&fds3);                        // azzero l'insieme puntato da fsd2 ( stdin )
            int res;

            f_to = FAILURE;
            tentativo = 0;

            do{

                if(tentativo>0) stampa_tentativo(tentativo);

                f_to = FAILURE;
                ack_d->n_seq = count;
                ack_d->size_frammento = SIZE_DOWN;

                //fprintf(stdout , "Invio frammento %d ...  \n" , p->n_seq);

                if( abort == 1 ) ack_d->n_seq = -1;

                //fprintf(stdout , "Invio ack %d ... \n" , ack_d->n_seq);
                sendto(s , ( t_ack_down * ) ack_d , sizeof(ack_d)  , 0 , (struct sockaddr *) &saddr , sizeof(saddr));

                if( abort == 1 ){
                    remove(file);
                    return(pTail);
                }
                tval3.tv_sec = SECONDI;                // secondi
                tval3.tv_usec = 0;                     // microsecondi
                FD_SET(s , &fds3);                     // aggiungo il socket a fds4

                n3 = select(FD_SETSIZE , &fds3 , NULL  , NULL , &tval3);

                if( n3 == -1 ){
                    fprintf(stderr , "Error in Select");
                    exit(1);
                }else if( n3 > 0 ){

                    res = recvfrom(s , ( t_pacchetto_down *) p , 2*sizeof(int)+SIZE_DOWN , 0 , 0 , 0 );
                    //fprintf(stdout , "Ricevo frammento %d ...\n" , p->n_seq);
                    if(p->ultimo == 1) flag = 1;
                    f_to = SUCCESS;
                    if(res == 0){
                        printf("Connessione chiusa\n");
                        exit(1);
                    }else if(res == -1){
                        printf("Recv Failed\n");
                        exit(1);
                    }

                   if(p->n_seq != count){
                        f_to = FAILURE;
                    }
                }

                tentativo++;

            }while(f_to == FAILURE && tentativo < N_TENTATIVI);

        }

        if(tentativo >= N_TENTATIVI){
            remove(file);
            fprintf(stderr , "\nErrore : Ack server non corretto oppure Server in crash...\n\n");
            return(pTail);
        }

        // INSERISCI MENU
        if(n == 2 && FD_ISSET(fileno(stdin) , &fds1) == 1){

            FD_SET(s , &fds2);

            fscanf(stdin , "%s" , cmd->operazione);
            if(strcmp(cmd->operazione , "UP")==0){
                fscanf(stdin , "%s" , cmd->nome_file);
                FILE *fp1;
                fp1 = apriFile(cmd->nome_file , "UP");
                if(fp1!=NULL){
                 pTail = enqueue(pTail , cmd);
                 fclose(fp1);
                }
                 // salvo nella lista il comando
                 fprintf(stdout , "comando inserito in coda ...\n");
                 //traversal(pTail);
            }else{
                if(strcmp(cmd->operazione , "DOWN")==0){
                    fscanf(stdin , "%s" , cmd->nome_file);
                    pTail = enqueue(pTail , cmd);

                    // salvo nella lista il comando

                    fprintf(stdout , "comando inserito in coda ...\n");
                    //traversal(pTail);
                }else{
                    // Comando ERRATO
                    if(strcmp(cmd->operazione , "fine")==0){
                        fprintf(stderr , "Trasferimento in corso : comando non disponibile!\n");
                    }else{
                        if(strcmp(cmd->operazione , "abort")==0){
                            abort = 1;
                        }else{
                            fprintf(stderr , "Errore : comando non disponibile!\n");
                        }
                    }
                }
            }
        }

        count++;
        fwrite(p->buf , sizeof(char) , SIZE_DOWN , fp);

    }while(flag==0);

    fprintf(stdout , "Fine Download file : %s\n" , file);
    fclose(fp);

   return(pTail);
}


t_coda *up(int s , struct sockaddr_in saddr , char *file , t_coda *pTail)
{

    // Apertura del file
    FILE *fp;

    fp = apriFile(file , "UP");    // apertura del file
    if(fp==NULL) return(pTail);    // errore apertura del file

    fprintf(stdout , "Inzio Upload file : %s ... \n" , file);

    t_pacchetto_up p[1];                    // pacchetto da inviare
    t_ack_up ack_p[1];


    int count = 1;                         // contatore di sequenza
    int b_letti;                           // bytes letti dal file
    int flag = 0;                          // flag per uscire dalla trasmissione per fine file

    t_comando cmd[1];

    // Gestione Select
    fd_set fds1 , fds2;                    // file descriptor per la select per gestire mpx
    int n;                                 // return della select
    struct timeval tval;                   // gestione timeout
    tval.tv_sec = 0;                       // secondi
    tval.tv_usec = 0;                      // microsecondi
    FD_ZERO(&fds1);                        // azzero l'insieme puntato da fsd2 ( stdin )
    FD_ZERO(&fds2);                        // azzero l'insieme puntato da fds3 ( socket )
    FD_SET(fileno(stdin) , &fds1);         // aggiungo lo stdin a fds1
    FD_SET(s , &fds2);                     // aggiungo il socket a fds1

    do{

        if(( n = select(s+1 , &fds1 , &fds2 , NULL , &tval)) == -1 ){
            fprintf(stderr , "Errore nella Select\n");
            close(s);
            exit(1);
        }

        // Preparazione pacchetto da inviare .....
        b_letti = fread(p->buf , sizeof(char) , MAX , fp);

        if ( b_letti < MAX  ) flag = 1;
        p[0].n_seq = count;
        p[0].ultimo = 0;
        // pacchetto p pronto da inviare .....

        int tentativo = 0;   // # tentativo della recvfrom
        int f_to;            // flag settato a success in caso di ricezione corretta


        if(FD_ISSET(s , &fds2) == 1){

            FD_SET(fileno(stdin) , &fds1);

            // Invio pacchetto ... attendo ack
            // se ack != count : ritrasmetto ( n_tentativi )
            // se scade timeout : ritrasmetto ( n_tentativi )

            fd_set fds3;                           // file descriptor per la select per gestire mpx
            int n3;                                // return della select
            struct timeval tval3;                  // gestione timeout
            FD_ZERO(&fds3);                        // azzero l'insieme puntato da fsd2 ( stdin )
            int res;

            do{

                if(tentativo>0) stampa_tentativo(tentativo);

                f_to = FAILURE;
                //fprintf(stdout , "Invio frammento %d ... \n" , p->n_seq);
                sendto(s , ( t_pacchetto_up *)p , sizeof(p) , 0 , (struct sockaddr *) &saddr , sizeof(saddr));

                tval3.tv_sec = SECONDI;                // secondi
                tval3.tv_usec = 0;                     // microsecondi
                FD_SET(s , &fds3);                     // aggiungo il socket a fds4

                n3 = select(FD_SETSIZE , &fds3 , NULL  , NULL , &tval3);

                if( n3== -1 ){
                    fprintf(stderr , "Error in Select");
                    exit(1);
                }else if( n3 > 0 ){
                    res = recvfrom(s , ( t_ack_up *) ack_p , sizeof(t_ack_up) , 0 , 0 , 0 );
                 //   fprintf(stdout , "Ricevo ack %d ... \n" , ack_p->n_seq);
                    f_to = SUCCESS;
                    if(res == 0){
                        printf("Connessione chiusa\n");
                        close(s);
                        exit(1);
                    }else if(res == -1){
                        printf("Recv Failed\n");
                        close(s);
                        exit(1);
                    }

                    if(ack_p->n_seq != count){   // ack errato
                        f_to = FAILURE;
                    }
                }

                tentativo++;

            }while(f_to == FAILURE && tentativo < N_TENTATIVI);

        }

        if(tentativo >= N_TENTATIVI){
            fprintf(stderr , "\nErrore ; Ack server non corretto oppure Server in crash...\n\n");
            return(pTail);
        }


        if(n == 2 && FD_ISSET(fileno(stdin) , &fds1) == 1){

            FD_SET(s , &fds2);
            fscanf(stdin , "%s" , cmd->operazione);

            if(strcmp(cmd->operazione , "UP")==0){
                fscanf(stdin , "%s" , cmd->nome_file);
                FILE *fp1;
                fp1 = apriFile(cmd->nome_file , "UP");
                if(fp1!=NULL){
                 pTail = enqueue(pTail , cmd);
                 fclose(fp1);
                }
                 // salvo nella lista il comando

                 fprintf(stdout , "comando inserito in coda ...\n");
                 //traversal(pTail);
            }else{
                if(strcmp(cmd->operazione , "DOWN")==0){
                    fscanf(stdin , "%s" , cmd->nome_file);
                    pTail = enqueue(pTail , cmd);

                    // salvo nella lista il comando
                    pTail = enqueue(pTail , cmd);
                    fprintf(stdout , "comando inserito in coda ...\n");
                    //traversal(pTail);
                }else{
                    // Comando ERRATO
                    if(strcmp(cmd->operazione , "fine")==0){
                        fprintf(stderr , "Trasferimento in corso: comando non disponibile!\n");
                    }else{
                        fprintf(stderr , "Errore : comando non disponibile!\n");
                    }
                }
            }
        }

        count++;

    }while(flag == 0);


    p->ultimo = 1;
    p->n_seq = count;
    memset(&(p->buf) , 0 , MAX*sizeof(char));

    //fprintf(stdout , "Invio frammento %d ... \n" , p->n_seq);
    sendto(s , ( t_pacchetto_up *)p , sizeof(p) , 0 , (struct sockaddr *) &saddr , sizeof(saddr));
    recvfrom(s , (t_ack_up *) ack_p , sizeof(t_ack_up) , 0 , 0 , 0 );
    //fprintf(stdout , "Ricevo ack %d ... \n" , ack_p->n_seq);


    if(ack_p->n_seq == 0) fprintf(stdout , "Fine Upload file : %s\n" , file);
    else fprintf(stdout , "File non inviato correttamente : ..... %d ......\n" , ack_p->n_seq);

    fclose(fp);

    return(pTail);
}


FILE *apriFile(char *nome_file , char *cmd){

    FILE *fp;

    if(strcmp(cmd , "UP")==0){
        if((fp=fopen(nome_file , "rb"))==NULL){
            fprintf(stderr , "Errore apertura del file.\n");
            return(NULL);
        }
    }else{
        if((fp=fopen(nome_file , "wb"))==NULL){
            fprintf(stderr , "Errore apertura del file.\n");
            return(NULL);
        }
    }
    return(fp);
}

// Funzioni per la gestione della coda

t_coda* newE ()
{
  t_coda *p;

  p = (t_coda *) malloc (sizeof (t_coda));

  if (p==NULL) {
    fprintf (stderr, "Allocazione fallita.");
    exit (FAILURE);
  }

  return (p);
}


t_coda *enqueue (t_coda *pTail, t_comando *cmd)
{
  t_coda *pNew;

  pNew = newE ();

  strcpy(pNew->cmd.operazione , cmd->operazione);
  strcpy(pNew->cmd.nome_file  , cmd->nome_file);


  if (pTail==NULL) {
    pTail = pNew;
    pTail->next = pTail;
 } else {
    pNew->next = pTail->next;
    pTail->next = pNew;
    pTail = pNew;
  }

  return (pTail);
}


t_coda *dequeue (t_coda *pTail, char *operazione , char *nome_file)
{
  t_coda *pOld;

  if (pTail != NULL) {
    if (pTail == pTail->next) {
      strcpy(operazione , pTail->cmd.operazione);
      strcpy(nome_file , pTail->cmd.nome_file);
      free (pTail);
      pTail = NULL;
    } else {
      pOld = pTail->next;
      strcpy(operazione , pOld->cmd.operazione);
      strcpy(nome_file, pOld->cmd.nome_file);
      pTail->next = pOld->next;
      free (pOld);
    }
  }

  return (pTail);
}

void traversal(t_coda *pTail)
{
  t_coda *pTmp;

  fprintf(stdout , "\n>> **** Coda Attuale **** :\n");

  fprintf (stdout, "pTail -> ");
  if (pTail == NULL) {
    fprintf (stdout, "NULL\n");
  } else {
    pTmp = pTail;
    do {
      fprintf (stdout, "( %s , %s ) -> ", pTmp->cmd.operazione , pTmp->cmd.nome_file);
      pTmp = pTmp->next;
    } while (pTmp != pTail);

    fprintf (stdout, "pTail \n");
  }

  fprintf(stdout , "\n");

  return;
}

void stampa_tentativo(int tentativo)
{
    fprintf(stderr , "\nTIMEOUT SCADUTO\n");
    fprintf(stderr , "Attenzione problema di connessione ...\n");
    fprintf(stderr , "tentativo di ristablire la connessione # (  %d \\ %d ) \n" , tentativo , N_TENTATIVI-1);
}
