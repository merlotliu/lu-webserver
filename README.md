# Web Server
一个简单的 web server 服务器

# v1.0 2022.10.12
- 服务器最核心的功能实现完成：
    - 线程池解决多用户连接的并发问题；
    - epoll I/O 多路复用；
    - 有限状态机解析简单的 HTTP Request；
- Tricks:
    - C 语言不定参数（va_list, va_start, va_end）;
    - 使用用了一些平时少用的 API，如：
        - strpbrk：在一个字符串中搜索集合中的任意字符；
        - strcasecmp：忽略大小写比较两个字符串；
        - strchr：定位到字符在字符串中的位置；
        - strspn：获取连续字符长度；
        - vsnprintf：类似 sprintf，详见 man 文档；
        - writev：将多个buffer内容写入一个文件描述符；
