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
httpd_send_block("2\r\n]}\r\n", 7, resp_arg);
httpd_send_block("0\r\n\r\n", 5, resp_arg);
