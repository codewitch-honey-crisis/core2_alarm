httpd_send_block("B\r\n{\"status\":[\r\n", 16, resp_arg);

for(size_t i = 0;i<alarm_count;++i) {
    if(i!=0) {
        
httpd_send_block("1\r\n,\r\n", 6, resp_arg);

    }
httpd_send_expr(alarm_values[i]?"true":"false", resp_arg);
}
httpd_send_block("2\r\n]}\r\n", 7, resp_arg);
httpd_send_block("0\r\n\r\n", 5, resp_arg);
