#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <rpc/xdr.h>


#include "types.h"

#define BUFSIZE 4000

#define SOCKET int
#define DATA_COUNT 3

/********************************************************************

    riceve il risultato

********************************************************************/
void riceviResult(SOCKET s, Response* my_resp){

    FILE *stream_socket_r;
    //Response resp;
    XDR xdrs_r;

    stream_socket_r = fdopen(s,"r");
    xdrstdio_create(&xdrs_r, stream_socket_r, XDR_DECODE);
    xdr_Response(&xdrs_r, my_resp);
    xdr_destroy(&xdrs_r);
	fclose(stream_socket_r);

//    *my_resp = resp;

}


/********************************************************************

    Scrive sul file "filename" il float result

********************************************************************/
void writeResultOnFile(char *filename, float result){

    FILE *fp;
    if( (fp=fopen(filename,"w"))==NULL ){
        printf(" --ERROR: cannot create file \"result.txt\"\n");
        exit(1);
    }
    fprintf(fp,"%f",result);
    fclose(fp);
    return;

}

/********************************************************************

    Legge count numeri dal file.
    Alloca un vettore di float lungo al max count e fa puntare
    il puntatore della struct Request al vettore allocato.

    Ritorna il numero di numeri letti e setta i campi della struct
    Request

********************************************************************/
int getDataFromFile(FILE *fp, Request* r, int count)
{

    int i;
    int letti = 0;
    float tmp = 0;
    float *data = (float*) calloc(count,sizeof(float));

    for (i=0 ; i<count ; i++)
    {

        if ( fscanf(fp,"%f",&tmp)==1 )
        {
            data[letti] = tmp;
            letti++;
        }
        else
        {
            break;
        }
    }
    r->data.data_val = data;
    r->data.data_len = letti;

    return letti;

}


/********************************************************************

    Legge esattamente nbytes al socket s.

    Ritorna il numero di bytes inviati o -1 se la send() fallisce

********************************************************************/
size_t my_recv(SOCKET s, void* buf, size_t nbytes)
{

    size_t res = 0;
    size_t bytes_rec = 0;

    for (bytes_rec=0 ; bytes_rec<nbytes ; bytes_rec+=res)
    {
        res = recv(s,buf+bytes_rec,1,0);
        if (res==-1)
            exit(1);
    }
    return bytes_rec;
}

/********************************************************************

	Riceve i valori da scrivere nella struttura request

********************************************************************/
void riempi_buffer(XDR* xdr, float f, bool_t last)
{

    if ( !xdr_float(xdr,&f) )
    {
        printf(" --ERROR: xdr_float() fail\n");
        exit(-1);
    }

    if ( !xdr_bool(xdr,&last) )
    {
        printf(" --ERROR: xdr_bool() fail\n");
        exit(-1);
    }
    return;
}

/********************************************************************

    Manda esattamente nbytes al socket s.

    Ritorna il numero di bytes inviati o -1 se la send() fallisce

********************************************************************/
size_t my_send(SOCKET s, void* data, size_t nbytes)
{

    size_t res = 0;
    size_t bytes_inviati = 0;

    for ( bytes_inviati=0 ; bytes_inviati<nbytes ; bytes_inviati+=res )
    {
        res = send(s,data+bytes_inviati,nbytes-bytes_inviati,0);
        if (res==-1)
            exit (-1);
    }
    return bytes_inviati;
}

/********************************************************************

	Riceve il nome del file. Ritorna il numero di righe

	param: 	filename --> nome del file

********************************************************************/
int getNumberOfRows(char* filename)
{

    FILE* fp=NULL;
    int righe=0;
    char buffer[1024];

    fp=fopen(filename,"r");
    if (fp==NULL)
        return -1;

    while (fgets(buffer,1024,fp))
        righe++;

    fclose(fp);
    return righe;
}
/********************************************************************

	Riceve il nome del file. Ritorna il numero di dati %f

	param: 	filename --> nome del file

********************************************************************/
int getNumberOfData(char* filename)
{

    FILE* fp=NULL;
    int dati=0;
    float dato;

    fp=fopen(filename,"r");
    if (fp==NULL)
        return -1;

    while (fscanf(fp,"%f",&dato)==1)
        dati++;

    fclose(fp);
    return dati;
}
/********************************************************************

	Riceve indirizzo e porta del server e prova a connettersi
	ad esso.
	Ritorna il socket connesso oppure -1 in caso di errore

	param: 	host --> indirizzo del server
			service --> porta sulla quale il server ascolta

********************************************************************/
SOCKET connectedTCP(char* host, char* service)
{

    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *res0;

    int s;	// socket

    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;              // IPv4
    hints.ai_socktype = SOCK_STREAM;        // socket stream
    hints.ai_protocol = IPPROTO_TCP;        // TCP

    /* getaddrinfo() ritorna 0 se tutto OK */
    if ( getaddrinfo(host, service, &hints, &res0) )
    {
        printf(" --ERROR: getaddrinfo() cannot resolve %s:%s\n", host, service);
        return -1;
    }

    s = -1;
    /* provo a creare un socket usando la lista dei risultati ottentuti dalla getaddrinfo() */
    for (res=res0; res!=NULL; res=res->ai_next )
    {

        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0)
        {
            //printf("socket() error: cannot create socket\n");
            continue;			// processa il prossimo indirizzo ricevuto dalla getaddrinfo()
        }
        if ( connect(s, res->ai_addr, res->ai_addrlen) < 0 )
        {
            //printf("connect() error: cannot connect to ip address\n");
            close(s);
            s = -1;
            continue;			// processa il prossimo indirizzo ricevuto dalla getaddrinfo()
        }
        break;					// se arrivo qui vuol dire che ho il socket connesso al server
    }

    freeaddrinfo(res0);
    return s;
}

int main(int argc, char** argv)
{
    SOCKET s;
    FILE *fp=NULL;

    int ndata = 0;  // conta il numero di dati nel file
    int nrow = 0;   // conta le righe del file

    Request xdr_req;
    Response xdr_resp;

    XDR xdrs_w;

    /* Controllo argomenti */
    if (argc != 4)
    {
        printf(" --ERROR, usage: %s <address> <port> <inputfile>\n", argv[0]);
        exit(1);
    }

    /* connessione al server */
    printf(" --connecting to %s:%s... ...\n", argv[1], argv[2]);
    s = connectedTCP(argv[1],argv[2]);
    if (s==-1)
    {
        printf(" --ERROR, cannot connect to %s:%s\n", argv[1], argv[2]);
        exit(1);
    }
    printf(" --connected... ...\n");

    /* ottengo il numero di righe e di %f del file che contiene la sequenza */
    ndata = getNumberOfData(argv[3]);
    nrow = getNumberOfRows(argv[3]);
    if ( ndata==-1 || nrow==-1 || ndata!=nrow)
    {
        printf(" --ERROR in format file \"%s\"\n", argv[3]);
        exit(1);
    }
    printf(" --DEBUG: file \"%s\" - %d lines\n", argv[3],nrow);

    /* apertura del file */
    fp=fopen(argv[3],"r");
    if (fp==NULL)
    {
        printf(" --ERROR, cannot open %s\n", argv[3]);
        exit(1);
    }

    /* stream per la Request */
    FILE *stream_socket_w = fdopen(s, "w");
    if (stream_socket_w == NULL)
    {
        printf(" --Error: w_fdopen() failed");
        exit(2);
    }

    /* invio dati al server */
    int inviati = 0;
    while ( inviati < nrow )
    {

        inviati += getDataFromFile(fp,&xdr_req,DATA_COUNT);

        if (inviati==nrow)
            xdr_req.last = TRUE;
        else
            xdr_req.last = FALSE;

        xdrstdio_create(&xdrs_w, stream_socket_w, XDR_ENCODE);
        //xdr_getpos(&xdrs_w);
        if ( !xdr_Request(&xdrs_w,&xdr_req) )
        {
            printf(" --Error: cannot ENCODE data. xdr_Request() failed\n");
            exit(1);
        }
		//sleep(1);
        fflush(stream_socket_w);
        free(xdr_req.data.data_val);
        xdr_destroy(&xdrs_w);
        

    }
	fclose(fp);
	
    printf(" --DEBUG: %d floats sent\n", inviati);
    printf(" --DEBUG: waiting for response...\n");

    riceviResult(s,&xdr_resp);
	fclose(stream_socket_w);
    /* scrivo il risultato sul file */
    if(xdr_resp.error==FALSE){
        writeResultOnFile("result.txt",xdr_resp.result);
        printf(" --DEBUG: \"result.txt\" correctly written... %f\n",xdr_resp.result);
        exit(0);
    } else {
        exit(1);
    }
	


    return 0;
}
