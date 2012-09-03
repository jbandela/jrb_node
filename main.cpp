//  Copyright John R. Bandela 2012
//
// Distributed under the Boost Software License, Version 1.0.
//    (See http://www.boost.org/LICENSE_1_0.txt)

#include "jrb_node.h"
#include <iostream>
#include <map>
#include <string>
#include <boost\test\unit_test.hpp>

using namespace jrb_node;
#ifdef JRB_NODE_SSL
boost::asio::ssl::context context_(boost::asio::ssl::context::sslv23_server);

#ifdef _MSC_VER 
//automatic linking on msvc to openssl
#pragma comment(lib,"ssleay32.lib")
#pragma comment(lib,"libeay32.lib")
#endif

#endif

int main()
{

	try
	{
		// the io_service used for the servers;
		boost::asio::io_service io_service;

		// create a simple http server - no ssl
		http_server server(io_service,9090);

		// here is our callback function - 
		server.accept([&io_service](request& req,response& res)->bool{
			// If there is a query then print it out
			std::map<std::string,std::string> m;
			req.parse_name_value(m);
			if(m.size()){
				std::string body_str;
				res.content_type("text");
				for(auto& p: m){
					body_str += "[" + p.first + "]=[" + p.second + "]\n";
				}
				res.body(body_str + req.method() + " " + req.url() + "\n" + jrb_node::name_value_to_string(m));
				return true;
			}
			//uses async_http_client to get boost license
			jrb_node::async_http_client client(jrb_node::uri("http://www.boost.org/LICENSE_1_0.txt"),io_service);
			client.get([res](const jrb_node::uri & uri, const jrb_node::client_response& r_client, boost::system::error_code ec)mutable{
				if(!ec){
					res.content_type(r_client.content_type());
					res.body(r_client.body());
					res.send();
				}
			});
			return false;
		});

#ifdef JRB_NODE_SSL
		// Now set up secure server
		// jrb.cer and .pkey are just self-signed certificates for localhost
		context_.use_certificate_file("jrb.cer",boost::asio::ssl::context_base::file_format::pem);
		context_.use_private_key_file("jrb.pkey",boost::asio::ssl::context_base::file_format::pem);

		// the ssl server
		https_server server_s(io_service,"127.0.0.1",9091,context_);

		// our callback - returns jquery license over https
		server_s.accept([](request& req,response& res)->bool{
			jrb_node::http_client client(jrb_node::uri("https://raw.github.com/jquery/jquery/master/MIT-LICENSE.txt"));
			auto r_client  = client.get();
			res.content_type(r_client.content_type());
			res.body(r_client.body());
			return true;
		});

#endif
		// run the io_service
		io_service.run();

	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}