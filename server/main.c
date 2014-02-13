/*
 
 Compile with:
 
    gcc -Wall -o server -lpthread *.c
 
 Run with:
 
    ./server <listen-port> <input-file>
 
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <rpc/xdr.h>

#include <string.h>

#include "types.h"


#define LISTENQ 15
#define MAXBUFL 4000


#define SOCKET int

/********************************************************************

    invia la response con error=TRUE

********************************************************************/
void inviaErrore(SOCKET s){

    FILE *stream_socket_w;
    Response resp;
    XDR xdrs_w;

    resp.error = TRUE;

    stream_socket_w = fdopen(s,"w");
    xdrstdio_create(&xdrs_w, stream_socket_w, XDR_ENCODE);
    xdr_Response(&xdrs_w, &resp);
    fflush(stream_socket_w);
    xdr_destroy(&xdrs_w);
    fclose(stream_socket_w);


}
/********************************************************************

    invia la response con  result e error=FALSE

********************************************************************/
void inviaResult(SOCKET s, float result){

    FILE *stream_socket_w;
    Response resp;
    XDR xdrs_w;

    resp.error = FALSE;
    resp.result = result;

    stream_socket_w = fdopen(s,"w");
    xdrstdio_create(&xdrs_w, stream_socket_w, XDR_ENCODE);
    xdr_Response(&xdrs_w, &resp);
    //size = xdr_getpos(&xdrs_w);
    fflush(stream_socket_w);
    xdr_destroy(&xdrs_w);
    fclose(stream_socket_w);

}



/********************************************************************

	handler per quando muore un figlio e manda un SIGCHLD

********************************************************************/
static void child_handler(int sig){

    pid_t pid;
    int status;

    while( (pid = waitpid(-1,&status,WNOHANG))>0 )
        ;

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

    Legge esattamente nbytes al socket s.

    Ritorna il numero di bytes inviati o -1 se la send() fallisce

********************************************************************/
int isLast(Request r)
{
    return (r.last==TRUE);
}

/********************************************************************

    Riceve la struct Request e evvettua i calcoli con i dati
    al suo interno.


********************************************************************/
float compute(Request* r, int* k, int v_server_data_count, float v_server_data[])
{
    int count = r->data.data_len;
    int offset = *k;
    int i;

    float result = 0;

    for (i=0 ; i<count ; i++){
        result += r->data.data_val[i]*v_server_data[(offset+i)%v_server_data_count];
        printf(" --DEBUG: ricevuto %f\n", r->data.data_val[i]);
    }
    *k = offset + count;
	//free(r.data.data_val);
    return result;

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
            return -1;
    }
    return bytes_rec;
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
            exit(1);
    }
    return bytes_inviati;
}

void serve_req (SOCKET connfd, struct sockaddr_in caddr, char *NameFileConf, float v_data_server[], int v_data_server_count)
{

    Request xdr_req;
    xdr_req.data.data_val = (float*) calloc(10*1024,sizeof(float));
    XDR xdrs_r;

    int k = 0;
    float result = 0;

    printf(" --Connected with %s:%d\n", inet_ntoa(caddr.sin_addr),ntohs(caddr.sin_port));
    FILE *stream_socket_r = fdopen(connfd, "r");
    if (stream_socket_r == NULL)
    {
        printf(" --Error: r_fdopen() failed");
        exit(1);
    }

    /* ricevo il primo dato */
    xdrstdio_create(&xdrs_r, stream_socket_r, XDR_DECODE);
    if ( !xdr_Request(&xdrs_r,&xdr_req) )
    {
        /* codifica e invio error=TRUE */
        inviaErrore(connfd);
    }
    xdr_destroy(&xdrs_r);
    result += compute(&xdr_req, &k, v_data_server_count, v_data_server);
	//free(xdr_req.data.data_val);
    //result += compute(xdr_req);

    /* ricevo gli altri dati */
    while ( !isLast(xdr_req) )
    {
        xdrstdio_create(&xdrs_r, stream_socket_r, XDR_DECODE);
        if ( !xdr_Request(&xdrs_r,&xdr_req) )
        {
            /* codifica e invio error=TRUE */
            inviaErrore(connfd);
        }
        xdr_destroy(&xdrs_r);
        result += compute(&xdr_req, &k, v_data_server_count, v_data_server);
		//free(xdr_req.data.data_val);
    }
    printf(" --DEBUG: result = %f\n",result);
    /* codifica e invio result */
    inviaResult(connfd, result);
	
	free(xdr_req.data.data_val);
    fclose(stream_socket_r);
    close(connfd);
    printf("-- Connection with %s:%d closed\n", inet_ntoa(caddr.sin_addr),ntohs(caddr.sin_port));
    exit(0);
    //return;
}



int main(int argc, char** argv)
{
    SOCKET s, s_conn;
    struct sockaddr_in saddr, caddr;

    float v_data_server[1000];
    int v_data_server_count = 0;

    int port, res;
    int i;
    socklen_t addrlen;
	
	int pid;	

    if (argc == 3)
    {
        port = atoi(argv[1]);
    }
    else
    {
        printf(" --ERROR: Wrong parameters; usage: %s port <inputfile>\n", argv[0]);
        exit(-1);
    }

    /* controllo righe file */
    int ndata = getNumberOfData(argv[2]);
    int nrow = getNumberOfRows(argv[2]);
    if ( ndata==-1 || nrow==-1 || ndata!=nrow)
    {
        printf(" --ERROR in format file \"%s\"\n", argv[2]);
        exit(1);

    }
    printf(" --DEBUG: file \"%s\" - %d lines\n", argv[2],nrow);

    /* apertura del file */
    FILE *fp=fopen(argv[2],"r");
    if (fp==NULL)
    {
        printf(" --ERROR, cannot open %s\n", argv[2]);
        exit(1);
    }

    /* lettura valori e salvataggio in v_data_server */
    v_data_server_count = nrow;
    for (i=0 ; i<1000 && i<v_data_server_count ; i++)
        fscanf(fp,"%f",&v_data_server[i]);

	fclose(fp);

    //signal(SIGCHLD,(void (*)(int))child_handler);


    s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == -1)
    {
        printf(" --ERROR: socket() failed\n");
        exit(-1);
    }

    // server address and port
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    res = bind(s, (struct sockaddr*)&saddr, sizeof(saddr));

    if (res == -1)
    {
        printf(" --ERROR: bind() failed\n");
        exit(-1);
    }

    res = listen(s, LISTENQ);
    if (res == -1)
    {
        printf(" --ERROR: listen() failed\n");
        exit(-1);
    }

    while (1)
    {

        // waiting for a incoming request of connection
        printf(" --Waiting for connections...\n");

        addrlen = sizeof(struct sockaddr_in);
        s_conn = accept(s, (struct sockaddr*)&caddr, &addrlen);

        if (s_conn == -1)
        {
            printf(" --ERROR: accept() failed\n");
            continue;
        }
		
		pid = fork();
		if(pid==0)	
            serve_req(s_conn, caddr, argv[2], v_data_server, v_data_server_count);
		else if(pid>0)
			signal(SIGCHLD,(void (*)(int))child_handler);
		else{
			printf(" --ERROR: fork() failed\n");
			exit(1);		
		}
    }

    close(s);
    return 0;
}
