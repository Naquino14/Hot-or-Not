#ifndef SMTP_CLIENT_H
#define SMTP_CLIENT_H

typedef struct {
    const char* to;
    const char* subject;
    const char* body;
} smtp_mail_t;

int smtp_set_server_ipaddr(const char* ipaddr, const char* hostname);

int smtp_client_send(const smtp_mail_t* mail);

int smtp_client_hello_auth();

#endif // !SMTP_CLIENT_H