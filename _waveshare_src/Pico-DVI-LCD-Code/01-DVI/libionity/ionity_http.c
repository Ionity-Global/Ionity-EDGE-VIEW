#include "ionity_http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

static struct tcp_pcb *http_pcb = NULL;
static struct tcp_pcb *http_clients[4] = {NULL};
static ionity_http_msg_handler_t msg_handler = NULL;

static ionity_message_t msg_queue[IONITY_HTTP_MAX_MSGS];
static int msg_count = 0;
static int msg_head = 0;

static const char HTTP_HEADER_OK[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n";

static const char HTTP_HEADER_JSON[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n"
    "Access-Control-Allow-Origin: *\r\n\r\n";

static const char HTTP_HEADER_303[] =
    "HTTP/1.1 303 See Other\r\n"
    "Location: /\r\n"
    "Connection: close\r\n\r\n";

static const char HTTP_HEADER_CORS[] =
    "HTTP/1.1 204 No Content\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Connection: close\r\n\r\n";

static const char HTML_PAGE[] =
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>IO-nity EDGE-VIEW</title>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{background:#0a0a0f;color:#e0e0e0;font-family:system-ui,sans-serif;"
    "min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px}"
    ".header{text-align:center;margin-bottom:30px}"
    ".header h1{color:#ff4444;font-size:2.5em;letter-spacing:4px;text-transform:uppercase}"
    ".header .sub{color:#44ff44;font-size:0.9em;margin-top:5px}"
    ".card{background:#1a1a2e;border:1px solid #333;border-radius:12px;padding:24px;"
    "width:100%;max-width:500px;margin-bottom:20px}"
    ".card h2{color:#ff4444;margin-bottom:15px;font-size:1.2em}"
    "input[type=text],textarea{width:100%;padding:12px;background:#0d0d1a;"
    "border:1px solid #444;border-radius:8px;color:#fff;font-size:1em;margin-bottom:10px}"
    "textarea{height:80px;resize:vertical}"
    "button{background:#ff4444;color:#fff;border:none;padding:12px 24px;"
    "border-radius:8px;font-size:1em;cursor:pointer;width:100%}"
    "button:hover{background:#ff6666}"
    ".msg{background:#0d0d1a;border-left:3px solid #ff4444;padding:10px 15px;"
    "margin-bottom:8px;border-radius:0 8px 8px 0}"
    ".msg .author{color:#ff4444;font-size:0.8em;font-weight:bold}"
    ".msg .time{color:#666;font-size:0.7em;float:right}"
    ".msg .text{color:#ccc;margin-top:4px;word-break:break-word}"
    ".status{display:flex;gap:10px;margin-top:10px;font-size:0.8em;color:#666}"
    ".status span{background:#1a1a2e;padding:5px 10px;border-radius:4px}"
    ".status .online{color:#44ff44}"
    "</style></head><body>"
    "<div class=\"header\">"
    "<h1>IO-NITY</h1>"
    "<div class=\"sub\">EDGE-VIEW &bull; Ionity Global (Pty) Ltd</div>"
    "</div>"
    "<div class=\"card\">"
    "<h2>Send Message to Display</h2>"
    "<form id=\"msgForm\">"
    "<input type=\"text\" id=\"author\" placeholder=\"Your name\" maxlength=\"30\" required>"
    "<textarea id=\"text\" placeholder=\"Type your message...\" maxlength=\"250\" required></textarea>"
    "<button type=\"submit\">Send to Display</button>"
    "</form>"
    "<div class=\"status\">"
    "<span id=\"connStatus\" class=\"online\">&#9679; Connected</span>"
    "<span id=\"msgCount\">0 messages</span>"
    "</div>"
    "</div>"
    "<div class=\"card\">"
    "<h2>Recent Messages</h2>"
    "<div id=\"messages\"><p style=\"color:#666\">No messages yet...</p></div>"
    "</div>"
    "<script>"
    "const form=document.getElementById('msgForm');"
    "const msgs=document.getElementById('messages');"
    "const count=document.getElementById('msgCount');"
    "let msgList=[];"
    "async function sendMsg(e){"
    "e.preventDefault();"
    "const author=document.getElementById('author').value.trim();"
    "const text=document.getElementById('text').value.trim();"
    "if(!author||!text)return;"
    "try{"
    "const r=await fetch('/message',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({author,text})"
    "});"
    "if(r.ok){"
    "document.getElementById('text').value='';"
    "loadMsgs();"
    "}"
    "}catch(err){"
    "document.getElementById('connStatus').innerHTML='&#9679; Offline';"
    "document.getElementById('connStatus').className='';"
    "}"
    "}"
    "async function loadMsgs(){"
    "try{"
    "const r=await fetch('/messages');"
    "const data=await r.json();"
    "msgList=data.messages||[];"
    "count.textContent=msgList.length+' messages';"
    "if(msgList.length===0){msgs.innerHTML='<p style=\"color:#666\">No messages yet...</p>';return}"
    "msgs.innerHTML=msgList.slice().reverse().map(m=>'<div class=\"msg\"><span class=\"author\">'+m.author.replace(/</g,'&lt;')+'</span><span class=\"time\">'+m.time+'</span><div class=\"text\">'+m.text.replace(/</g,'&lt;')+'</div></div>').join('');"
    "document.getElementById('connStatus').innerHTML='&#9679; Connected';"
    "document.getElementById('connStatus').className='online';"
    "}catch(err){"
    "document.getElementById('connStatus').innerHTML='&#9679; Offline';"
    "document.getElementById('connStatus').className='';"
    "}"
    "}"
    "form.addEventListener('submit',sendMsg);"
    "loadMsgs();"
    "setInterval(loadMsgs,5000);"
    "</script></body></html>";

static void close_http(struct tcp_pcb *pcb) {
    for (int i = 0; i < 4; i++) {
        if (http_clients[i] == pcb) { http_clients[i] = NULL; break; }
    }
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_close(pcb);
}

static void close_http_err(void *arg, err_t err) {
    (void)err;
    close_http((struct tcp_pcb *)arg);
}

static void http_send(struct tcp_pcb *pcb, const char *header, const char *body) {
    tcp_write(pcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
    if (body) tcp_write(pcb, body, strlen(body), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void http_serve_messages(struct tcp_pcb *pcb) {
    char json[4096];
    int pos = snprintf(json, sizeof(json), "{\"messages\":[");
    bool first = true;
    for (int i = 0; i < msg_count; i++) {
        int idx = (msg_head - msg_count + i + IONITY_HTTP_MAX_MSGS) % IONITY_HTTP_MAX_MSGS;
        if (!first) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        first = false;
        char author_safe[64], text_safe[512];
        int a = 0, t = 0;
        for (int j = 0; msg_queue[idx].author[j] && a < 63; j++) {
            char c = msg_queue[idx].author[j];
            if (c == '"' || c == '\\') author_safe[a++] = '\\';
            author_safe[a++] = c;
        }
        author_safe[a] = '\0';
        for (int j = 0; msg_queue[idx].text[j] && t < 511; j++) {
            char c = msg_queue[idx].text[j];
            if (c == '"' || c == '\\') text_safe[t++] = '\\';
            text_safe[t++] = c;
        }
        text_safe[t] = '\0';
        pos += snprintf(json + pos, sizeof(json) - pos,
                        "{\"author\":\"%s\",\"text\":\"%s\",\"time\":%lu}",
                        author_safe, text_safe, msg_queue[idx].timestamp);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    http_send(pcb, HTTP_HEADER_JSON, json);
}

static void http_handle_post(struct tcp_pcb *pcb, const char *body) {
    char author[32] = "Anonymous";
    char text[IONITY_HTTP_MAX_MSG_LEN] = "";

    char *a = strstr(body, "\"author\":\"");
    if (a) {
        a += 10;
        int i = 0;
        while (*a && *a != '"' && i < 31) author[i++] = *a++;
        author[i] = '\0';
    }

    char *t = strstr(body, "\"text\":\"");
    if (t) {
        t += 8;
        int i = 0;
        while (*t && *t != '"' && i < IONITY_HTTP_MAX_MSG_LEN - 1) text[i++] = *t++;
        text[i] = '\0';
    }

    if (text[0]) {
        ionity_http_msg_push(text, author);
        if (msg_handler) msg_handler(text, author);
    }

    http_send(pcb, HTTP_HEADER_303, NULL);
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        close_http(pcb);
        return ERR_OK;
    }

    char buf[1024];
    uint16_t len = p->tot_len;
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    pbuf_copy_partial(p, buf, len, 0);
    buf[len] = '\0';

    if (strncmp(buf, "OPTIONS", 7) == 0) {
        http_send(pcb, HTTP_HEADER_CORS, NULL);
    } else if (strncmp(buf, "GET /messages", 13) == 0) {
        http_serve_messages(pcb);
    } else if (strncmp(buf, "POST /message", 13) == 0) {
        char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            http_handle_post(pcb, body_start + 4);
        } else {
            http_send(pcb, HTTP_HEADER_303, NULL);
        }
    } else if (strncmp(buf, "GET / ", 6) == 0 || strncmp(buf, "GET /", 5) == 0) {
        http_send(pcb, HTTP_HEADER_OK, HTML_PAGE);
    } else {
        http_send(pcb, HTTP_HEADER_303, NULL);
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    for (int i = 0; i < 4; i++) {
        if (http_clients[i] == NULL) {
            http_clients[i] = new_pcb;
            tcp_recv(new_pcb, http_recv);
            tcp_err(new_pcb, close_http_err);
            return ERR_OK;
        }
    }
    tcp_close(new_pcb);
    return ERR_OK;
}

bool ionity_http_init(uint16_t port) {
    http_pcb = tcp_new();
    if (!http_pcb) return false;

    err_t err = tcp_bind(http_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        tcp_close(http_pcb);
        http_pcb = NULL;
        return false;
    }

    http_pcb = tcp_listen(http_pcb);
    tcp_accept(http_pcb, http_accept);
    return true;
}

void ionity_http_set_msg_handler(ionity_http_msg_handler_t handler) {
    msg_handler = handler;
}

void ionity_http_msg_push(const char *text, const char *author) {
    strncpy(msg_queue[msg_head].text, text, IONITY_HTTP_MAX_MSG_LEN - 1);
    msg_queue[msg_head].text[IONITY_HTTP_MAX_MSG_LEN - 1] = '\0';
    strncpy(msg_queue[msg_head].author, author, 31);
    msg_queue[msg_head].author[31] = '\0';
    msg_queue[msg_head].timestamp = to_ms_since_boot(get_absolute_time()) / 1000;
    msg_head = (msg_head + 1) % IONITY_HTTP_MAX_MSGS;
    if (msg_count < IONITY_HTTP_MAX_MSGS) msg_count++;
}

int ionity_http_msg_count(void) {
    return msg_count;
}

const ionity_message_t *ionity_http_msg_get(int index) {
    if (index < 0 || index >= msg_count) return NULL;
    int idx = (msg_head - msg_count + index + IONITY_HTTP_MAX_MSGS) % IONITY_HTTP_MAX_MSGS;
    return &msg_queue[idx];
}

void ionity_http_msg_clear(void) {
    msg_count = 0;
    msg_head = 0;
}

void ionity_http_send_json(const char *json) {
    for (int i = 0; i < 4; i++) {
        if (http_clients[i]) {
            http_send(http_clients[i], HTTP_HEADER_JSON, json);
        }
    }
}