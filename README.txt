WHAT IS JRB NODE

This library provides a boost.asio based sync and async http_client and http and https server 

The http_parser library from joyent is used to parse http (https://github.com/joyent/http-parser/)

RATIONALE

Currently there is no boost asio https server open source library. I wanted to see if I could make a simple library.
I re-used http_parser so that I did not have to debug async parsing of http
Then I built up templated classes to handle reading http requests into headers and body and reused those classes


LICENSE

Boost license for jrb_node

MIT license of http_parser

USING
Needs boost and boost asio and boost threads. Tested with boost 1.49
Openssl needs to be linked unless JRB_NODE_NO_SSL is defined

Include jrb_node.cpp http_parser.cpp in your project and include jrb_node.h 

An example program is provided in main.cpp

all components are in namespace jrb_node

COMPILERS
Compiles and runs with MSVC 2012 RC and mingw gcc 4.7.1 (nuwen.net distro)
