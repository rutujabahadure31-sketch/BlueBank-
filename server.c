#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_ACCOUNTS 500
#define DATA_FILE "accounts.txt"
#define REQUEST_SIZE 16384
#define BODY_SIZE 30000

typedef struct { int number; char name[80]; double balance; } Account;
static Account accounts[MAX_ACCOUNTS];
static int count = 0;
static int nextNumber = 100001;

static void loadAccounts(void) {
    FILE *f = fopen(DATA_FILE, "r");
    if (!f) return;
    while (count < MAX_ACCOUNTS && fscanf(f, "%d|%79[^|]|%lf\n", &accounts[count].number, accounts[count].name, &accounts[count].balance) == 3) {
        if (accounts[count].number >= nextNumber) nextNumber = accounts[count].number + 1;
        count++;
    }
    fclose(f);
}
static void saveAccounts(void) {
    FILE *f = fopen(DATA_FILE, "w"); if (!f) return;
    for (int i=0; i<count; i++) fprintf(f, "%d|%s|%.2f\n", accounts[i].number, accounts[i].name, accounts[i].balance);
    fclose(f);
}
static void logTransaction(int number, const char *type, double amount) {
    FILE *f = fopen("transactions.txt", "a"); if (!f) return;
    fprintf(f, "%d|%s|%.2f\n", number, type, amount); fclose(f);
}
static int findAccount(int number) { for (int i=0; i<count; i++) if (accounts[i].number == number) return i; return -1; }
static void getParameter(const char *query, const char *key, char *out, size_t size) {
    out[0]='\0'; if (!query || size < 2) return;
    char search[100]; snprintf(search,sizeof(search),"%s=",key); const char *p=query;
    while ((p=strstr(p,search)) != NULL) { if (p==query || p[-1]=='&') break; p += strlen(search); }
    if (!p) return; p += strlen(search); size_t i=0;
    while (*p && *p!='&' && i<size-1) {
        if (*p=='+') { out[i++]=' '; p++; }
        else if (*p=='%' && p[1] && p[2]) { char hex[3]={p[1],p[2],0}; char *end; long v=strtol(hex,&end,16); if (*end || v==0) { out[0]=0; return; } out[i++]=(char)v; p+=3; }
        else out[i++]=*p++;
    }
    out[i]=0;
    if (strchr(out,'|') || strchr(out,'\r') || strchr(out,'\n')) out[0]=0;
}
static void sendResponse(int client, int status, const char *reason, const char *body) {
    char header[1024]; int len=(int)strlen(body);
    int h=snprintf(header,sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\nAccess-Control-Max-Age: 86400\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n", status,reason,len);
    if (h>0) send(client,header,(size_t)h,0); send(client,body,(size_t)len,0);
}
static void respond(int c,int status,const char *body) {
    const char *r=status==200?"OK":status==400?"Bad Request":status==404?"Not Found":status==409?"Conflict":status==500?"Internal Server Error":status==503?"Service Unavailable":"Error";
    sendResponse(c,status,r,body);
}
static void handleRequest(int client, char *request) {
    char method[10]="", path[2048]="", query[2048]="", body[BODY_SIZE]="";
    if (sscanf(request,"%9s %2047s",method,path)!=2) { respond(client,400,"{\"error\":\"Invalid request\"}"); return; }
    if (!strcmp(method,"OPTIONS")) { respond(client,200,"{}"); return; }
    if (strcmp(method,"GET")) { respond(client,400,"{\"error\":\"Use GET for this demo\"}"); return; }
    char *q=strchr(path,'?'); if(q){*q=0; strncpy(query,q+1,sizeof(query)-1);}
    if (!strcmp(path,"/")) { respond(client,200,"{\"message\":\"BlueBank API is running\",\"demo\":true}"); return; }
    if (!strcmp(path,"/api/accounts")) {
        int used=snprintf(body,sizeof(body),"{\"accounts\":[");
        for(int i=0;i<count && used<(int)sizeof(body)-128;i++) used+=snprintf(body+used,sizeof(body)-(size_t)used,"%s{\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",i?",":"",accounts[i].number,accounts[i].name,accounts[i].balance);
        if(used<(int)sizeof(body)-3){snprintf(body+used,sizeof(body)-(size_t)used,"]}");respond(client,200,body);} else respond(client,500,"{\"error\":\"Too many accounts to display\"}"); return;
    }
    if (!strcmp(path,"/api/create")) {
        char name[80], initialText[40]; getParameter(query,"name",name,sizeof(name)); getParameter(query,"deposit",initialText,sizeof(initialText));
        char *end=NULL; double initial=strtod(initialText,&end);
        if(!name[0] || !initialText[0] || !end || *end || initial<0 || initial>1000000000.0 || count>=MAX_ACCOUNTS){respond(client,400,"{\"error\":\"Enter a name and a valid non-negative initial deposit\"}");return;}
        for(size_t i=0;name[i];i++) if(name[i]=='"'||name[i]=='\\'){respond(client,400,"{\"error\":\"Name contains an unsupported character\"}");return;}
        accounts[count].number=nextNumber++; snprintf(accounts[count].name,sizeof(accounts[count].name),"%s",name); accounts[count].balance=initial; count++; saveAccounts();
        if(initial>0) logTransaction(accounts[count-1].number,"CREATE_DEPOSIT",initial);
        snprintf(body,sizeof(body),"{\"message\":\"Account created\",\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",accounts[count-1].number,name,initial); respond(client,200,body); return;
    }
    if (!strcmp(path,"/api/deposit") || !strcmp(path,"/api/withdraw") || !strcmp(path,"/api/balance")) {
        char numText[40], amountText[40]; getParameter(query,"account",numText,sizeof(numText)); char *end=NULL; long number=strtol(numText,&end,10);
        if(!numText[0] || !end || *end || number<=0 || number>2147483647L){respond(client,400,"{\"error\":\"Invalid account number\"}");return;}
        int index=findAccount((int)number); if(index<0){respond(client,404,"{\"error\":\"Account not found\"}");return;}
        if(!strcmp(path,"/api/balance")){snprintf(body,sizeof(body),"{\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",accounts[index].number,accounts[index].name,accounts[index].balance);respond(client,200,body);return;}
        getParameter(query,"amount",amountText,sizeof(amountText)); end=NULL; double amount=strtod(amountText,&end);
        if(!amountText[0] || !end || *end || amount<=0 || amount>1000000000.0){respond(client,400,"{\"error\":\"Enter a valid positive amount\"}");return;}
        if(!strcmp(path,"/api/deposit")){if(accounts[index].balance+amount>1000000000.0){respond(client,400,"{\"error\":\"Balance limit exceeded\"}");return;} accounts[index].balance+=amount;logTransaction((int)number,"DEPOSIT",amount);}
        else {if(accounts[index].balance<amount){respond(client,400,"{\"error\":\"Insufficient balance\"}");return;}accounts[index].balance-=amount;logTransaction((int)number,"WITHDRAW",amount);}
        saveAccounts(); snprintf(body,sizeof(body),"{\"message\":\"Transaction successful\",\"number\":%d,\"balance\":%.2f}",accounts[index].number,accounts[index].balance);respond(client,200,body);return;
    }
    if (!strcmp(path,"/api/transactions")) {
        char numText[40];getParameter(query,"account",numText,sizeof(numText));char *end=NULL;long number=strtol(numText,&end,10);
        if(!numText[0]||!end||*end||number<=0||findAccount((int)number)<0){respond(client,404,"{\"error\":\"Account not found\"}");return;}
        FILE *f=fopen("transactions.txt","r");int used=snprintf(body,sizeof(body),"{\"transactions\":[");char line[256];int first=1;
        if(f){while(fgets(line,sizeof(line),f)){int n;char type[40];double amount;if(sscanf(line,"%d|%39[^|]|%lf",&n,type,&amount)==3&&n==(int)number&&used<(int)sizeof(body)-100){used+=snprintf(body+used,sizeof(body)-(size_t)used,"%s{\"type\":\"%s\",\"amount\":%.2f}",first?"":",",type,amount);first=0;}}fclose(f);}
        snprintf(body+used,sizeof(body)-(size_t)used,"]}");respond(client,200,body);return;
    }
    respond(client,404,"{\"error\":\"Endpoint not found\"}");
}
int main(void) {
    const char *portEnv=getenv("PORT"); int port=portEnv?atoi(portEnv):8080; if(port<1||port>65535)port=8080;
    int server=socket(AF_INET,SOCK_STREAM,0); if(server<0){perror("socket");return 1;}
    int opt=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);addr.sin_port=htons((unsigned short)port);
    if(bind(server,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");close(server);return 1;}
    if(listen(server,32)<0){perror("listen");close(server);return 1;}
    loadAccounts();printf("BlueBank demo API listening on port %d; loaded %d accounts\n",port,count);fflush(stdout);
    for(;;){int client=accept(server,NULL,NULL);if(client<0){if(errno==EINTR)continue;perror("accept");continue;}char request[REQUEST_SIZE];ssize_t n=recv(client,request,sizeof(request)-1,0);if(n>0){request[n]=0;handleRequest(client,request);}close(client);}
    close(server);return 0;
}
