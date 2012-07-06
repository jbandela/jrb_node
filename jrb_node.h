//  Copyright John R. Bandela 2012
//
// Distributed under the Boost Software License, Version 1.0.
//    (See http://www.boost.org/LICENSE_1_0.txt)
// Portions adapted from examples and documentation for Boost.Asio Copyright © 2003-2012 Christopher M. Kohlhoff

#ifndef JRB_NODE_HPP_2012_06_29
#define JRB_NODE_HPP_2012_06_29

#define BOOST_ASIO_HAS_MOVE
#ifndef JRB_NODE_NO_SSL
#define JRB_NODE_SSL 
#endif
#include <map>
#include <string>
#include <boost/asio.hpp>
#include <boost/system/system_error.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <boost/lexical_cast.hpp>

#ifdef JRB_NODE_SSL
#include<boost/asio/ssl.hpp>
#endif

namespace jrb_node{

	struct uri{
		void schema(const std::string& str){schema_ = str;}
		const std::string& schema()const {return schema_;}

		void host(const std::string& str){host_ = str;}
		const std::string& host()const {return host_;}

		void port(const std::string& str){port_ = str;}
		void port(int p){port_ = boost::lexical_cast<std::string>(p);}
		const std::string& port(){return port_;}

		void path(const std::string& str){ path_ = str;}
		const std::string& path(){return path_;}

		void query(const std::string& str){ query_ = str;}
		const std::string& query(){return query_;}

		void fragment(const std::string& str){ fragment_ = str;}
		const std::string& fragment(){return fragment_;}

		std::string get_uri_string(){return schema_ + "://" + host_ + if_present(port_,":") + path_
			+ if_present(query_,"?") + if_present(fragment_,"#");

		}

		std::string get_uri_client_request_string(){
			return path_ + if_present(query_,"?");
		}
		void set_uri_string(const std::string& str);

		bool valid();

		void check_valid(){if(!valid()) throw std::runtime_error("Invalid uri");}

		uri(){}
		uri(const std::string& str){set_uri_string(str);}

	private:
		std::string schema_;
		std::string host_;
		std::string port_;
		std::string path_;
		std::string query_;
		std::string fragment_;
		static std::string if_present(const std::string& part,const std::string& sep){
			if(part.size()){
				return sep + part;
			}else{
				return std::string();
			}
		}
	};

	struct http_message{
	private:
		// from boost examples
		struct iless
		{
			bool operator()(std::string const& x,
				std::string const& y) const
			{
				return boost::algorithm::ilexicographical_compare(x, y, std::locale());
			}
		};

	public:
		typedef std::map<std::string,std::string,iless> map_type;

		std::string& operator[](const std::string& key){return headers_[key];}

		const std::string& body()const{return body_;}
		void body(const std::string& b){ body_ =  b;}
		void body_append(const std::string& b){ body_ +=  b;}

		const std::string& method()const {return method_;}
		void method(const std::string& m) { method_ = m;}

		const std::string& url()const{return url_;}
		void url(const std::string& u){ url_ = u;}

		map_type& headers(){return headers_;}
		const map_type& headers()const{return headers_;}

	private:
		std::string method_;
		std::string body_;
		std::string url_;
		map_type headers_;



	};
	struct status_t{
		enum status_type
		{
			ok = 200,
			created = 201,
			accepted = 202,
			no_content = 204,
			multiple_choices = 300,
			moved_permanently = 301,
			moved_temporarily = 302,
			not_modified = 304,
			bad_request = 400,
			unauthorized = 401,
			forbidden = 403,
			not_found = 404,
			internal_server_error = 500,
			not_implemented = 501,
			bad_gateway = 502,
			service_unavailable = 503
		} status_;
		status_t():status_(ok){}
		const std::string& get_status_http_string(status_type s)const;
		const std::string& get_status_http_string()const {return get_status_http_string(status_);}



		boost::asio::const_buffer to_buffer() const{return boost::asio::buffer(get_status_http_string(status_));}

	};

	struct jrb_parser_message;

	struct request{
	private:
		std::shared_ptr<jrb_parser_message> ptr_;
	public:
		request();
		request(std::shared_ptr<jrb_parser_message>p);
		std::string body()const;
		int status_code()const;
		const http_message::map_type& headers();
		std::string content_type(){
			auto iter = headers().find("content-type");
			if (iter == headers().end()){
				return "";
			}
			else{
				return iter->second;
			}
		}
	};

	typedef request client_response;

	struct response{
	protected:
		http_message message_;
		status_t status_;

	public:
		response(){}
		void body(const std::string& s){return message_.body(s);}
		const std::string& body()const{message_.body();}

		void content_type(const std::string & s){
			message_["Content-Type"] = 	s;

		}
		std::string content_type()const{
			auto iter = message_.headers().find("Content-Type");
			if(iter == message_.headers().end()){
				return "";
			}else{
				return iter->second;
			}
		}

		status_t::status_type status()const{return status_.status_;}
		void status(status_t::status_type t){status_.status_ = t;}

	};
	struct response_derived:public response{
		void add_required_headers(){
			message_["Content-Length"] = boost::lexical_cast<std::string>(message_.body().size());
			if(message_.headers().count("Content-Type") == 0){
				message_["Content-Type"] = 	"text/html";
			}

		}
		std::string get_as_http();
	};




	template <class  AsyncReadStream> 
	struct jrb_stream_reader;

	class http_server
	{
	public:
		typedef jrb_node::jrb_stream_reader<boost::asio::ip::tcp::socket> stream_reader;
		typedef std::shared_ptr<stream_reader> connection_ptr;
		typedef std::function<bool (request&, response&, const boost::system::error_code& )> handler_func;
		typedef std::function<bool (request&, response&)> simple_handler_func;
		typedef std::function<void (const boost::system::error_code&) > simple_error_func;


		http_server(boost::asio::io_service& io_service, int port)
			: acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
		{
		}

		http_server(boost::asio::io_service& io_service,const std::string& ip, int port)
			: acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ip), port))
		{
		}


		void accept_ec(handler_func func);
		void accept(simple_handler_func f){
			simple_error_func ef = error_func_;
			handler_func func = [f,ef](request& req,  response& res, const boost::system::error_code& ec)->bool{
				if(!ec){
					return f(req,res);
				}
				else{
					if(ef){
						ef(ec);
					}
					return false;
				}
			};
			accept_ec(func);
		}
		void set_error_function(simple_error_func func){error_func_ = func;}
	private:
		boost::asio::ip::tcp::acceptor acceptor_;
		simple_error_func error_func_;

	};

#ifdef JRB_NODE_SSL

	class https_server
	{
	public:
		typedef jrb_node::jrb_stream_reader<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> stream_reader;
		typedef std::shared_ptr<stream_reader> connection_ptr;
		typedef std::function<bool (request&, response&, const boost::system::error_code& )> handler_func;
		typedef std::function<bool (request&, response&)> simple_handler_func;
		typedef std::function<void (const boost::system::error_code&) > simple_error_func;


		https_server(boost::asio::io_service& io_service, int port,boost::asio::ssl::context& c)
			: acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),context_(c)
		{
		}

		https_server(boost::asio::io_service& io_service,const std::string& ip, int port,boost::asio::ssl::context& c)
			: acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ip), port)),context_(c)
		{
		}
		void accept_ec(handler_func func);
		void accept(simple_handler_func f){
			simple_error_func ef = error_func_;
			handler_func func = [f,ef](request& req,  response& res, const boost::system::error_code& ec)->bool{
				if(!ec){
					return f(req,res);
				}
				else{
					if(ef){
						ef(ec);
					}
					return false;
				}
			};
			accept_ec(func);
		}
		void set_error_function(simple_error_func func){error_func_ = func;}
	private:
		boost::asio::ip::tcp::acceptor acceptor_;
		boost::asio::ssl::context& context_;
		simple_error_func error_func_;
	};

#endif

	template<class SocketType=boost::asio::ip::tcp::socket>
	struct async_http_client_holder;



	struct async_http_client{
		typedef jrb_node::jrb_stream_reader<boost::asio::ip::tcp::socket> stream_reader;
		typedef std::shared_ptr<stream_reader> connection_ptr;
		typedef std::function<bool (request&, response&, const boost::system::error_code& )>  s_handler_func;


		typedef std::function<void(const uri&, const client_response&, const boost::system::error_code& )> handler_func; 

		async_http_client(const uri& u, boost::asio::io_service& io);
		void get(handler_func f)const;
		void post(const std::string& data,const std::string& content_type,handler_func f);
		void set_uri(const uri& u);
		const uri& get_uri(){return uri_;};

		uri uri_;
		boost::asio::io_service* io_;
	};

	struct http_client{

		http_client(const uri& u, boost::asio::io_service& io):client_(u,io){}
		http_client(const uri& u);
		client_response get();
		client_response post(const std::string& data,const std::string& content_type);
		void set_uri(const uri& u){client_.set_uri(u);}
		const uri& get_uri(){return client_.get_uri();}
		std::unique_ptr<boost::asio::io_service> ptr_;
		async_http_client client_;

	};

	bool is_short_read(const boost::system::error_code& error);
}

#endif