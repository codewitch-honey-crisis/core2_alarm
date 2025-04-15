httpd_send_block("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\nB\r\n{\"status\":[\r\n", 95, resp_arg);

for(size_t i = 0;i<alarm_count;++i) {
    bool b=alarm_values[i];
    
httpd_send_expr(i==0?(b?"true":"false"):(b?",true":",false"), resp_arg);
}
httpd_send_block("2\r\n]}\r\n", 7, resp_arg);
httpd_send_block("0\r\n\r\n", 5, resp_arg);
