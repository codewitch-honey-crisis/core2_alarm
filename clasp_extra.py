Import("env")

print("ClASP Suite integration enabled")

env.Execute("dotnet clasptree.dll web ./include/httpd_content.h /prefix httpd_ /epilogue ./include/httpd_epilogue.h /state resp_arg /block httpd_send_block /expr httpd_send_expr /handlers extended")