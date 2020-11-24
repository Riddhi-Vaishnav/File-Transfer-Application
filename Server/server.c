/*
* code: File Transfer Application - Server
* project: Advanced Systems Programming
* by: Riddhi Vaishnav,
*     Maulik Tailor 
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>

// global vars
char rootDir[] = "./dump/";

// custom flags
char getAction[] = "-1";
char putAction[] = "-2";
char fileFoundAction[] = "@#$%";
char fileNotFoundAction[] = "-2";
char ack[] = "-ack";

// custom messages
char exitMsg[] = "quit\n";
char wellcomeMsg[] = "Welcome to File Server!\n=========================================\nPlease try commands like\n > get <fileName> - to download,\n > put <filename> - to upload or\n > quit - to exit\n=========================================";
char errorMsg[] = "Invalid command, please try again!";
char foundMsg[] = "I have the file!";
char notFoundMsg[] = "I don't have the file!";
char downloadMsg[] = "Please wait till I download on your machine!";

void serviceClient(int);
void getFile(char *file, int sd);
int putFile(char *file, int sd);
int processRequest(char msg[], int sd);
bool getAck(int sd);
void sendAck(int sd);

int main(int argc, char *argv[])
{
    int sd, client, portNumber, status;
    struct sockaddr_in socAddr;

    // if there are not enough arguements
    if (argc != 2)
    {
        printf("Synopsis: %s <Port Number>\n", argv[0]);
        exit(0);
    }

    // creating socket descriptor
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }

    // creating socket address
    socAddr.sin_family = AF_INET;
    socAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    // on port no - portNumber
    sscanf(argv[1], "%d", &portNumber);
    socAddr.sin_port = htons((uint16_t)portNumber);

    // binding socket to socket addr
    bind(sd, (struct sockaddr *)&socAddr, sizeof(socAddr));

    // listening on that socket with max backlog of 5 connections
    listen(sd, 5);

    while (1)
    {
        // block process until a client appears on this socket
        client = accept(sd, NULL, NULL);

        // let the child process handle the rest
        if (!fork())
        {
            printf("Client/%d is connected!\n", getpid());
            serviceClient(client);
        }

        close(client);
    }
}

void serviceClient(int sd)
{
    char message[255];
    int n;

    write(sd, wellcomeMsg, sizeof(wellcomeMsg));

    while (1)
        // reads message from client
        if (n = read(sd, message, 255))
        {
            message[n] = '\0';
            printf("Client/%d: %s", getpid(), message);
            if (!strcasecmp(message, exitMsg))
            {
                printf("Client/%d is disconnected!\n", getpid());
                close(sd);
                exit(0);
            }

            processRequest(message, sd);
        }
}

int processRequest(char msg[], int sd)
{
    char *command, *fileName;

    // copying original msg to save it from mutating
    char tempMessage[255];
    strcpy(tempMessage, msg);

    // tokenizing every command with space - " " & '\n'
    int i = 0;
    char *tokens = strtok(tempMessage, " \n");
    while (tokens != NULL && i < 2)
    {
        if (i == 0)
            command = tokens;
        else
            fileName = tokens;

        i++;
        tokens = strtok(NULL, " \n");
    }

    // improper command arguement- no file name
    if (i != 2)
    {
        write(sd, errorMsg, sizeof(errorMsg));
        return 0;
    }

    // verify requested command
    if (strcasecmp(command, "get") == 0)
        getFile(fileName, sd);
    else if (strcasecmp(command, "put") == 0)
        putFile(fileName, sd);
    else
    {
        write(sd, errorMsg, sizeof(errorMsg));
        return 0;
    }
}

void getFile(char *file, int sd)
{
    printf("> getFile Called: %s\n", file);

    char dirName[255];
    strcpy(dirName, rootDir);
    // defining required var
    DIR *dp;
    dp = opendir(dirName);
    struct dirent *dirp;
    bool isFound = false;

    // read all dirs
    while ((dirp = readdir(dp)) != NULL)
    {
        // find that file
        if (strcmp(dirp->d_name, file) == 0 && dirp->d_type != DT_DIR)
        {
            isFound = true;
            printf("> '%s' found\n", file);
            // write(sd, foundMsg, sizeof(foundMsg));

            // after this, client will be in - donwloadFile() method
            write(sd, getAction, sizeof(getAction));
            printf("> downloadFile() in client called\n", file);
            getAck(sd);

            // sending file name
            write(sd, file, strlen(file));
            printf("> fileName sent\n");
            getAck(sd);

            strcat(dirName, file);
            int fd = open(dirName, O_RDONLY, S_IRUSR | S_IWUSR);

            long fileSize = lseek(fd, 0L, SEEK_END);

            // send filesize
            write(sd, &fileSize, sizeof(long));
            printf("> fileSize sent\n");
            getAck(sd);

            // create buffer of fileSize
            char *buffer = malloc(fileSize);
            lseek(fd, 0L, SEEK_SET);
            read(fd, buffer, fileSize);
            write(sd, buffer, fileSize);
            getAck(sd);

            free(buffer);
            close(fd);
            printf("> '%s' sent\n", file);
        }
    }
    if (!isFound)
    {
        printf("> '%s' not found\n", file);
        write(sd, notFoundMsg, sizeof(notFoundMsg));
    }

    closedir(dp);
}

int putFile(char *file, int sd)
{
    int n;
    char message[255];
    char dirName[255];

    strcpy(dirName, rootDir);
    printf("> putFile Called: %s\n", file);

    // after this, client will be in - uploadFile() method
    write(sd, putAction, sizeof(putAction));
    getAck(sd);

    write(sd, file, strlen(file));
    printf("> fileName sent\n", file);
    getAck(sd);

    read(sd, message, 255);
    sendAck(sd);

    if (strcmp(message, fileFoundAction) == 0)
    {
        strcat(dirName, file);
        int fd = open(dirName, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);

        long fileSize;
        read(sd, &fileSize, sizeof(long));
        printf("> fileSize: %ld\n", fileSize);
        sendAck(sd);

        char *buffer = malloc(fileSize);
        read(sd, buffer, fileSize);
        write(fd, buffer, fileSize);
        sendAck(sd);

        free(buffer);
        close(fd);

        printf("> '%s' download complete!!!\n", file);
    }
    else if (strcmp(message, fileNotFoundAction) == 0)
    {
        printf("> File does not exist on client side\n");
    }
}

void sendAck(int sd)
{
    write(sd, ack, sizeof(ack));
}
bool getAck(int sd)
{
    char ackMsg[sizeof(ack)];
    read(sd, ackMsg, sizeof(ackMsg));
    if (strcmp(ack, ackMsg) == 0)
        return true;
    else
        return false;
}