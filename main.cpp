//  Copyright John R. Bandela 2012
//
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "jrb_node.h"
#include <boost/thread.hpp>
#include <iostream>

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

		// here is our callback function - uses http_client to get boost license
		server.accept([](request& req,response& res,const boost::system::error_code& ec)->bool{
			if(!ec){
				jrb_node::http_client client(jrb_node::uri("http://www.boost.org/LICENSE_1_0.txt"));
				auto r_client = client.get();
				res.content_type(r_client.content_type());
				res.body(r_client.body());
				return true;
			}
			else{
				std::string s = ec.message();
				std::cerr << s;
				return false;
			}
		});

#ifdef JRB_NODE_SSL
		// Now set up secure server
		// jrb.cer and .pkey are just self-signed certificates for localhost
		context_.use_certificate_file("jrb.cer",boost::asio::ssl::context_base::file_format::pem);
		context_.use_private_key_file("jrb.pkey",boost::asio::ssl::context_base::file_format::pem);

		// the ssl server
		https_server server_s(io_service,9091,context_);

		// our callback - returns results for google search for boost
		server_s.accept([](request& req,response& res,const boost::system::error_code& ec)->bool{
			if(!ec){
				jrb_node::http_client client(jrb_node::uri("https://raw.github.com/jquery/jquery/master/MIT-LICENSE.txt"));
				auto r_client  = client.get();
				res.content_type(r_client.content_type());
				res.body(r_client.body());
				return true;
			}
			else{
				std::string s = ec.message();
				std::cerr << s;
				return false;
			}
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