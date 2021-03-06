/*
Author: Igor Kashin
Project: POP3 Server
Company: BMSTU
Description: server.c
*/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include "logger.h"
#include "server.h"


void server_init(void)
{
    server.fStop = 0;
    STAILQ_INIT( &server.sclients.cliqueue );
    server.sclients.maxfd = -1;
    mqlog( "Server initialized");
}

void server_start(int port)
{
    int servfd = -1;
    int fAdded = 0;
    struct client *cliiter = NULL;

    mqlog( "Wanna start serving at port %d %d", port, server.fStop);
    if ((servfd = pop3_bindsock(port)) < 0) {
        server.fStop = 1;
        mqlog( "Error while setup socket. Escape from server routing." );
        return;
    }

    client_add(servfd);
    while (1) {
        FD_ZERO(&server.sclients.read_set);
        server.sclients.maxfd = -1;
        STAILQ_FOREACH(cliiter, &server.sclients.cliqueue, next) {
            if (server.sclients.maxfd < cliiter->fd) server.sclients.maxfd = cliiter->fd;
            FD_SET( cliiter->fd, &server.sclients.read_set);
        }
        server.sclients.maxfd++;

        mqlog( "Selecting %d...", server.sclients.maxfd );
        /*will be watched to see if characters  become  available  for
       reading  (more  precisely, to see if a read will not block; in particu-
       lar, a file descriptor is also ready on end-of-file)*/

       if (pselect(server.sclients.maxfd, &server.sclients.read_set, NULL, NULL, NULL, NULL) == -1) {
            mqlog( "Error in select(), but maybe signal arrived. server.fStop = %d", server.fStop );
            if (server.fStop == 1) break;
            continue;  //continue while
        }

        mqlog( "Selecting done..." );
        STAILQ_FOREACH(cliiter, &server.sclients.cliqueue, next) {
            if (FD_ISSET(cliiter->fd, &server.sclients.read_set)) {
                if (cliiter->fd == servfd) {
                    handle_client(client_add(client_accept(servfd)), 1);
                    break;
                } else {
                    mqlog( "Need client_handle_request for %d", cliiter->fd );
                    if (handle_client(cliiter->fd, 0) <= 0) {
                        client_remove( cliiter );
                        break;
                    }
                }
            }
        }
    }
}

int client_add(int fd)
{
    struct client *pNewClient = (struct client*)malloc(sizeof(struct client));

    pNewClient->fd = fd;
    STAILQ_INSERT_TAIL(&server.sclients.cliqueue, pNewClient, next);
    if (server.sclients.maxfd < fd)
        server.sclients.maxfd = fd;
    mqlog( "Client %d added", fd);
    return fd;
}

void client_remove(struct client *pClient)
{
    int i = 0;

    mqlog( "Client %d remove", pClient->fd);
    STAILQ_REMOVE(&server.sclients.cliqueue, pClient, client, next);
    free(pClient);
}

int client_accept(int serverfd)
{
    int i, ret;
    socklen_t t;
    struct sockaddr_in remote;

    t = sizeof(remote);
    ret = accept(serverfd, (struct sockaddr *)&remote, &t);
    if (ret == -1) {
        mqlog( "Error while accept()" );
        exit(1);
    }
    mqlog( "Connected %i", ret);
    return ret;
}

int pop3_bindsock(int port)
{
    int sl, optval = 1;
    struct sockaddr_in sin;

    if ((sl = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        mqlog( "Error while socket() = %d", sl );
        return -1;
    }

    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family=AF_INET;
    sin.sin_port=htons(port);
    sin.sin_addr.s_addr=htonl(INADDR_ANY);
    setsockopt(sl, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    fcntl(sl, O_NONBLOCK);

    if (bind(sl, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == -1) {
        mqlog( "Error while bind()" );
        close(sl);
        return -1;
    }

    if (listen(sl, DEF_MAXPENDING) == -1) {
        mqlog( "Error while listen()" );
        close(sl);
        return -1;
    }

    return sl;
}
