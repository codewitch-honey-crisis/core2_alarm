httpd_send_block("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n108\r\n<!DOCTYPE html>\r\n<html>\r\n    <head>\r\n        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\r\n        <title>Alarm Control Panel</title>\r\n    </head>\r\n    <body>\r\n        <h1>Alarm Control Panel</h1>\r\n        <form method=\"get\" action=\".\">\r\n", 343, resp_arg);
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
httpd_send_block("A5\r\n\r\n            <input type=\"submit\" name=\"set\" value=\"set\" />\r\n            <input type=\"submit\" name=\"refresh\" value=\"get\" />\r\n        </form>\r\n    </body>\r\n</html>\r\n\r\n", 171, resp_arg);
httpd_send_block("0\r\n\r\n", 5, resp_arg);
