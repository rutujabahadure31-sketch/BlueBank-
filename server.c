
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define MAX_ACCOUNTS 500
#define DATA_FILE "accounts.txt"

typedef struct {
    int number;
    char name[80];
    double balance;
} Account;

Account accounts[MAX_ACCOUNTS];
int count = 0;
int nextNumber = 100001;

void loadAccounts() {
    FILE *file = fopen(DATA_FILE, "r");
    if (file == NULL) return;

    while (count < MAX_ACCOUNTS &&
           fscanf(file, "%d|%79[^|]|%lf\n",
                  &accounts[count].number,
                  accounts[count].name,
                  &accounts[count].balance) == 3) {
        if (accounts[count].number >= nextNumber)
            nextNumber = accounts[count].number + 1;
        count++;
    }

    fclose(file);
}

void saveAccounts() {
    FILE *file = fopen(DATA_FILE, "w");
    if (file == NULL) return;

    for (int i = 0; i < count; i++) {
        fprintf(file, "%d|%s|%.2f\n",
                accounts[i].number,
                accounts[i].name,
                accounts[i].balance);
    }

    fclose(file);
}

void logTransaction(int number, const char *type, double amount) {
    FILE *file = fopen("transactions.txt", "a");
    if (file == NULL) return;

    fprintf(file, "%d|%s|%.2f\n", number, type, amount);
    fclose(file);
}

int findAccount(int number) {
    for (int i = 0; i < count; i++) {
        if (accounts[i].number == number)
            return i;
    }
    return -1;
}

void getParameter(const char *query, const char *key,
                  char *output, size_t size) {
    output[0] = '\0';
    if (query == NULL) return;

    char search[100];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(query, search);

    if (p == NULL || (p != query && p[-1] != '&'))
        return;

    p += strlen(search);
    size_t i = 0;

    while (*p && *p != '&' && i < size - 1) {
        if (*p == '+') {
            output[i++] = ' ';
            p++;
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], '\0'};
            char *end;
            long value = strtol(hex, &end, 16);

            if (*end != '\0' || value == 0) {
                output[0] = '\0';
                return;
            }

            output[i++] = (char)value;
            p += 3;
        } else {
            output[i++] = *p++;
        }
    }

    output[i] = '\0';

    if (strchr(output, '|') || strchr(output, '\r') ||
        strchr(output, '\n'))
        output[0] = '\0';
}

void respond(SOCKET client, int status, const char *body) {
    char header[512];
    const char *label = status == 200 ? "OK" :
                        status == 400 ? "Bad Request" :
                        status == 404 ? "Not Found" :
                        status == 409 ? "Conflict" :
                        status == 500 ? "Server Error" :
                        "Error";

    int length = (int)strlen(body);

    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, label, length);

    send(client, header, (int)strlen(header), 0);
    send(client, body, length, 0);
}

void handleRequest(SOCKET client, char *request) {
    char method[10] = "";
    char path[2048] = "";
    char query[2048] = "";
    char body[16000] = "";

    if (sscanf(request, "%9s %2047s", method, path) != 2) {
        respond(client, 400, "{\"error\":\"Invalid request\"}");
        return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        respond(client, 200, "{}");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        respond(client, 400, "{\"error\":\"Use GET for this demo\"}");
        return;
    }

    char *question = strchr(path, '?');
    if (question != NULL) {
        *question = '\0';
        strncpy(query, question + 1, sizeof(query) - 1);
    }

    if (strcmp(path, "/") == 0) {
        respond(client, 200,
            "{\"message\":\"BlueBank API is running\"}");
        return;
    }

    if (strcmp(path, "/api/accounts") == 0) {
        int used = snprintf(body, sizeof(body),
                            "{\"accounts\":[");
        for (int i = 0; i < count && used < (int)sizeof(body); i++) {
            used += snprintf(body + used, sizeof(body) - used,
                "%s{\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",
                i ? "," : "",
                accounts[i].number,
                accounts[i].name,
                accounts[i].balance);
        }

        if (used < (int)sizeof(body) - 3) {
            snprintf(body + used, sizeof(body) - used, "]}");
            respond(client, 200, body);
        } else {
            respond(client, 500, "{\"error\":\"Too many accounts to display\"}");
        }
        return;
    }

    if (strcmp(path, "/api/create") == 0) {
        char name[80];
        char initialText[40];
        getParameter(query, "name", name, sizeof(name));
        getParameter(query, "deposit", initialText, sizeof(initialText));

        char *end;
        double initial = strtod(initialText, &end);

        if (name[0] == '\0' || strlen(name) > 79 ||
            initialText[0] == '\0' || *end != '\0' ||
            initial < 0 || initial > 1000000000.0 ||
            count >= MAX_ACCOUNTS) {
            respond(client, 400,
                "{\"error\":\"Enter a name and a valid non-negative initial deposit\"}");
            return;
        }

        for (int i = 0; name[i]; i++) {
            if (name[i] == '"' || name[i] == '\\') {
                respond(client, 400,
                    "{\"error\":\"Name contains an unsupported character\"}");
                return;
            }
        }

        accounts[count].number = nextNumber++;
        strcpy(accounts[count].name, name);
        accounts[count].balance = initial;
        count++;

        saveAccounts();
        if (initial > 0)
            logTransaction(accounts[count - 1].number, "CREATE_DEPOSIT", initial);

        snprintf(body, sizeof(body),
            "{\"message\":\"Account created\",\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",
            accounts[count - 1].number, name, initial);
        respond(client, 200, body);
        return;
    }

    if (strcmp(path, "/api/deposit") == 0 ||
        strcmp(path, "/api/withdraw") == 0 ||
        strcmp(path, "/api/balance") == 0) {

        char numberText[40];
        char amountText[40];
        getParameter(query, "account", numberText, sizeof(numberText));

        char *end;
        long number = strtol(numberText, &end, 10);

        if (numberText[0] == '\0' || *end != '\0' || number <= 0) {
            respond(client, 400, "{\"error\":\"Invalid account number\"}");
            return;
        }

        int index = findAccount((int)number);

        if (index < 0) {
            respond(client, 404, "{\"error\":\"Account not found\"}");
            return;
        }

        if (strcmp(path, "/api/balance") == 0) {
            snprintf(body, sizeof(body),
                "{\"number\":%d,\"name\":\"%s\",\"balance\":%.2f}",
                accounts[index].number, accounts[index].name,
                accounts[index].balance);
            respond(client, 200, body);
            return;
        }

        getParameter(query, "amount", amountText, sizeof(amountText));
        double amount = strtod(amountText, &end);

        if (amountText[0] == '\0' || *end != '\0' ||
            amount <= 0 || amount > 1000000000.0) {
            respond(client, 400,
                "{\"error\":\"Enter a valid positive amount\"}");
            return;
        }

        if (strcmp(path, "/api/deposit") == 0) {
            if (accounts[index].balance + amount > 1000000000.0) {
                respond(client, 400, "{\"error\":\"Balance limit exceeded\"}");
                return;
            }
            accounts[index].balance += amount;
            logTransaction((int)number, "DEPOSIT", amount);
        } else {
            if (accounts[index].balance < amount) {
                respond(client, 400,
                    "{\"error\":\"Insufficient balance\"}");
                return;
            }
            accounts[index].balance -= amount;
            logTransaction((int)number, "WITHDRAW", amount);
        }

        saveAccounts();
        snprintf(body, sizeof(body),
            "{\"message\":\"Transaction successful\",\"number\":%d,\"balance\":%.2f}",
            accounts[index].number, accounts[index].balance);
        respond(client, 200, body);
        return;
    }

    if (strcmp(path, "/api/transactions") == 0) {
        char numberText[40];
        getParameter(query, "account", numberText, sizeof(numberText));

        char *end;
        long number = strtol(numberText, &end, 10);

        if (numberText[0] == '\0' || *end != '\0' ||
            findAccount((int)number) < 0) {
            respond(client, 404, "{\"error\":\"Account not found\"}");
            return;
        }

        FILE *file = fopen("transactions.txt", "r");
        int used = snprintf(body, sizeof(body), "{\"transactions\":[");
        char line[256];
        int first = 1;

        if (file != NULL) {
            while (fgets(line, sizeof(line), file) != NULL) {
                int transactionNumber;
                char type[40];
                double amount;

                if (sscanf(line, "%d|%39[^|]|%lf",
                    &transactionNumber, type, &amount) == 3 &&
                    transactionNumber == (int)number) {

                    used += snprintf(body + used, sizeof(body) - used,
                        "%s{\"type\":\"%s\",\"amount\":%.2f}",
                        first ? "" : ",", type, amount);
                    first = 0;

                    if (used >= (int)sizeof(body) - 100)
                        break;
                }
            }
            fclose(file);
        }

        snprintf(body + used, sizeof(body) - used, "]}");
        respond(client, 200, body);
        return;
    }

    respond(client, 404, "{\"error\":\"Endpoint not found\"}");
}

int main(void) {
    WSADATA wsa;
    SOCKET server;
    struct sockaddr_in address;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);

    if (bind(server, (struct sockaddr *)&address,
             sizeof(address)) == SOCKET_ERROR) {
        printf("Could not bind to port %d.\n", PORT);
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, 10) == SOCKET_ERROR) {
        printf("Could not start listening.\n");
        closesocket(server);
        WSACleanup();
        return 1;
    }

    loadAccounts();

    printf("BlueBank API is running on http://127.0.0.1:%d\n", PORT);
    printf("Loaded %d account(s).\n", count);
    printf("Press Ctrl+C to stop.\n");

    while (1) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        char request[8192];
        int received = recv(client, request, sizeof(request) - 1, 0);

        if (received > 0) {
            request[received] = '\0';
            handleRequest(client, request);
        }

        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
