// Generated with clasptree
// To use this file, define WWW_CONTENT_IMPLEMENTATION in exactly one translation unit (.c/.cpp file) before including this header.
#ifndef WWW_CONTENT_H
#define WWW_CONTENT_H

#define HTTPD_RESPONSE_HANDLER_COUNT 4
typedef struct { const char* path; const char* path_encoded; void (* handler) (void* arg); } httpd_response_handler_t;
extern httpd_response_handler_t httpd_response_handlers[4];
#ifdef __cplusplus
extern "C" {
#endif

// ./index.clasp
void httpd_www_content_index_clasp(void* resp_arg);
// ./api/index.clasp
void httpd_www_content_api_index_clasp(void* resp_arg);

#ifdef __cplusplus
}
#endif

#endif // WWW_CONTENT_H

#ifdef WWW_CONTENT_IMPLEMENTATION

httpd_response_handler_t httpd_response_handlers[4] = {
    { "/", "/", httpd_www_content_index_clasp },
    { "/index.clasp", "/index.clasp", httpd_www_content_index_clasp },
    { "/api", "/api", httpd_www_content_api_index_clasp },
    { "/api/index.clasp", "/api/index.clasp", httpd_www_content_api_index_clasp }
};
void httpd_www_content_index_clasp(void* resp_arg) {
    httpd_send_block("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n108\r\n<!D"
        "OCTYPE html>\r\n<html>\r\n    <head>\r\n        <meta name=\"viewport\" content=\"width=d"
        "evice-width, initial-scale=1.0\" />\r\n        <title>Alarm Control Panel</title>\r\n"
        "    </head>\r\n    <body>\r\n        <h1>Alarm Control Panel</h1>\r\n        <form met"
        "hod=\"get\" action=\".\">\r\n", 343, resp_arg);
    for(size_t i = 0;i<alarm_count;++i) {
                
    httpd_send_block("15\r\n\r\n            <label>\r\n", 27, resp_arg);
    httpd_send_expr(i+1, resp_arg);
    httpd_send_block("2F\r\n</label><input name=\"a\" type=\"checkbox\" value=\"\r\n", 53, resp_arg);
    httpd_send_expr(i, resp_arg);
    httpd_send_block("2\r\n\" \r\n", 7, resp_arg);
    if(alarm_values[i]){
    httpd_send_block("8\r\nchecked \r\n", 13, resp_arg);
    }
    httpd_send_block("8\r\n/><br />\r\n", 13, resp_arg);
    
    }
    httpd_send_block("A5\r\n\r\n            <input type=\"submit\" name=\"set\" value=\"set\" />\r\n            <i"
        "nput type=\"submit\" name=\"refresh\" value=\"get\" />\r\n        </form>\r\n    </body>\r\n"
        "</html>\r\n\r\n0\r\n\r\n", 176, resp_arg);
    free(resp_arg);
}
void httpd_www_content_api_index_clasp(void* resp_arg) {
    httpd_send_block("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\nB"
        "\r\n{\"status\":[\r\n", 95, resp_arg);
    
    for(size_t i = 0;i<alarm_count;++i) {
        bool b=alarm_values[i];
        if(i==0) {
            if(b) {
                
    httpd_send_block("4\r\ntrue\r\n", 9, resp_arg);
    
            } else {
                
    httpd_send_block("5\r\nfalse\r\n", 10, resp_arg);
    
            }
        } else {
            if(b) {
                
    httpd_send_block("5\r\n,true\r\n", 10, resp_arg);
    
            } else {
                
    httpd_send_block("6\r\n,false\r\n", 11, resp_arg);
    
            }
        }
    }
    httpd_send_block("2\r\n]}\r\n0\r\n\r\n", 12, resp_arg);
    free(resp_arg);
}
#endif // WWW_CONTENT_IMPLEMENTATION
