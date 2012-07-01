//  Copyright John R. Bandela 2012
//
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "External/http_parser.h"
#include "jrb_node.h"	

#include <iostream>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/thread/future.hpp>
#include <boost/make_shared.hpp>

#ifdef JRB_NODE_SSL
#include <boost/asio/ssl.hpp>
#endif


namespace jrb_node{

	struct jrb_parser_message:public http_parser{
		http_message message_;
		std::string current_header_;
		std::string last_header_;
	};

	namespace{
		void jrb_shutdown_helper(boost::asio::ip::tcp::socket& s, boost::system::error_code& ec){
			s.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			s.close();

		}
#ifdef JRB_NODE_SSL
		void jrb_shutdown_helper(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& s, boost::system::error_code& ec){
			s.shutdown(ec); // this seems to cause a hang in mingw gcc 4.7.1
			jrb_shutdown_helper(s.next_layer(),ec);


		}
#endif


	}

	// request methods
	request::request(): ptr_(){}
	request::request(boost::shared_ptr<jrb_parser_message>p): ptr_(p){}
	std::string request::body() const{return ptr_->message_.body();}
	int request::status_code()const {return ptr_->status_code;}
	const http_message::map_type& request::headers(){return ptr_->message_.headers();}

	
	// helper function
	bool is_short_read(const boost::system::error_code& error){
		bool value;
#ifdef JRB_NODE_SSL
		return (error.category() == boost::asio::error::get_ssl_category() && 
			error.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ));
#else
		return false;
#endif

	}

	template <class  AsyncReadStream>
	struct jrb_stream_reader :public jrb_parser_message,public boost::enable_shared_from_this<jrb_stream_reader<AsyncReadStream>>{
		typedef std::function<bool (request&, response&, const boost::system::error_code& )> handler_func;
		handler_func handler_;
		typedef boost::shared_ptr<AsyncReadStream> s_type;
		s_type s_;
		http_parser_settings settings;
		std::array<char,8192> buffer_;
		int total_bytes_;
		bool finished_;
		typename AsyncReadStream::lowest_layer_type& socket(){return s_->lowest_layer();}



		void start(){		  
			auto ptr = this->shared_from_this();
			s_->async_read_some(boost::asio::buffer(buffer_),[ptr]( const boost::system::error_code& error,  std::size_t bytes_transferred )->void{
				ptr->handle_read(error,bytes_transferred);
			});
		}
		jrb_stream_reader(s_type s,handler_func f):s_(s),handler_(f),total_bytes_(0),finished_(false){init();}
		jrb_stream_reader(boost::asio::io_service& io, handler_func f):s_(new AsyncReadStream(io)),handler_(f),total_bytes_(0),finished_(false){
			init();
		}

		template<class T, class U>
		jrb_stream_reader(T&& t, U&& u, handler_func f):s_(new AsyncReadStream(std::forward<T>(t),std::forward<U>(u))),handler_(f),total_bytes_(0),finished_(false){
			init();
		}


		void init(){
			++counter;
			settings = http_parser_settings();
			http_parser_init(this, HTTP_BOTH);
			settings.on_message_complete = [](http_parser* p){
				jrb_stream_reader<AsyncReadStream>* pm = static_cast<jrb_stream_reader<AsyncReadStream>*>(p);
				pm->finished_ = true;
				if(pm->total_bytes_){
					request req(pm->shared_from_this());
					response_derived res;
					boost::system::error_code ec;
					if(pm->handler_(req,res,ec)){

						auto str = boost::make_shared<std::string>(res.get_as_http());
						auto ptr = pm->shared_from_this();
						pm->s_->async_write_some(boost::asio::buffer(*str),[ptr,str](const boost::system::error_code& e,  std::size_t bytes_transferred ){ 
							if(e){
								request req;
								response res;
								ptr->handler_(req,res,e);
								boost::system::error_code ec;
								jrb_shutdown_helper(*ptr->s_,ec);

							}else{
								boost::system::error_code ec;
								jrb_shutdown_helper(*ptr->s_,ec);
							}
						});
					}
				}


				return 0;

			};


			settings.on_url = [](http_parser* p, const char *at, size_t length)->int{
				jrb_parser_message* pm = static_cast<jrb_parser_message*>(p);
				pm->message_.url(pm->message_.url() + std::string(at,length));
				return 0;
			};
			settings.on_header_value = [](http_parser *p, const char *at, size_t length)->int{
				jrb_parser_message* pm = static_cast<jrb_parser_message*>(p);
				if(pm->current_header_.size()){
					pm->last_header_ = std::move(pm->current_header_);
					pm->current_header_.clear();
				}
				pm->message_[pm->last_header_] += std::string(at,length);
				return 0;

			};
			settings.on_header_field = [](http_parser *p, const char *at, size_t length)->int{
				jrb_parser_message* pm = static_cast<jrb_parser_message*>(p);
				pm->current_header_ += std::string(at,length);
				return 0;
			};
			settings.on_body = [](http_parser *p, const char *at, size_t length)->int{
				jrb_parser_message* pm = static_cast<jrb_parser_message*>(p);
				pm->message_.body_append( std::string(at,length));
				return 0;
			};


		}

		void handle_read( const boost::system::error_code& error,  std::size_t bytes_transferred ){
			total_bytes_+= bytes_transferred;
			if(is_short_read(error) && total_bytes_==0){
				// close the connection
				boost::system::error_code ec;
				jrb_shutdown_helper(*s_,ec);
			}
			else if(error  == boost::asio::error::eof || is_short_read(error) ){ // boost returns short read for ssl termination	
					if(bytes_transferred){
						const char* data = buffer_.data();
						if(http_parser_execute(this,&settings,data,bytes_transferred) != bytes_transferred){
							// error parsing
							request req;
							response res;
							handler_(req,res,boost::system::errc::make_error_code(boost::system::errc::bad_message));
						}

					}
					if(finished_) return;
					char a = 0;
					if(http_parser_execute(this,&settings,&a,0) != 0 || finished_==false){
							// error parsing or 0 read
							request req;
							response res;
							handler_(req,res,error);
					}
			}
			else if(error){
				request req;
				response res;
				handler_(req,res,error);
			}
			else{
				if(bytes_transferred){
					const char* data = buffer_.data();
					if(http_parser_execute(this,&settings,data,bytes_transferred) != bytes_transferred){
							// error parsing
							request req;
							response res;
							handler_(req,res,boost::system::errc::make_error_code(boost::system::errc::bad_message));
					}

				}
				if(finished_) return;
				auto ptr =this->shared_from_this();
				s_->async_read_some(boost::asio::buffer(buffer_),[ptr]( const boost::system::error_code& error,  std::size_t bytes_transferred )->void{
					ptr->handle_read(error,bytes_transferred);
				});

			}

		}
		static int counter;
		~jrb_stream_reader(){
			--counter;
//			std::cout << --counter << "\n";
		}




	};

	template <class  AsyncReadStream> 
	int jrb_stream_reader<AsyncReadStream>::counter = 0;

	void http_server::accept(handler_func func)
	{
		connection_ptr new_connection(new stream_reader(acceptor_.get_io_service(),func));

		acceptor_.async_accept(*new_connection->s_,[this,new_connection,func](const boost::system::error_code& error){
			accept(func);
			if (!error)
			{
				new_connection->start();
			}


		});
	}

#ifdef JRB_NODE_SSL
	void https_server::accept(handler_func func)
	{
		typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;
		boost::shared_ptr<ssl_socket> s(new ssl_socket(acceptor_.get_io_service(),context_));

		connection_ptr new_connection(new stream_reader(s,func));

		acceptor_.async_accept(new_connection->socket(),[this,new_connection,func,s](const boost::system::error_code& error)mutable{
			accept(func);

			if(!error){
				s->async_handshake(boost::asio::ssl::stream_base::server,[this,new_connection,func,s](const boost::system::error_code& error)mutable{

					if (!error)
					{
						new_connection->start();
					}
					else{
						request req;
						response res;
						func(req,res,error);
						boost::system::error_code ec;
						jrb_shutdown_helper(*new_connection->s_,ec);
					}

				}); // async handshake
			}
			else{
				request req;
				response res;
				func(req,res,error);
				boost::system::error_code ec;
				jrb_shutdown_helper(*new_connection->s_,ec);

			}


		}); // async accept
	}

#endif

	namespace{
		template<class Func>
		void async_do_client_handshake(const std::string& host, boost::asio::ip::tcp::socket& sock, Func f){
			boost::system::error_code ec;
			f(ec);
		}

#ifdef JRB_NODE_SSL
		template<class Func>
		void async_do_client_handshake(const std::string& host, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& sock, Func f){

			using boost::asio::ip::tcp;
			namespace ssl = boost::asio::ssl;
			typedef ssl::stream<tcp::socket> ssl_socket;
			sock.lowest_layer().set_option(tcp::no_delay(true));
			sock.set_verify_mode(ssl::verify_none);
			sock.async_handshake(ssl_socket::client,f);
		}

#endif

	}

	template<class SocketType>
	struct async_http_client_holder:public boost::enable_shared_from_this<async_http_client_holder<SocketType>>{
		typedef std::function<void(const uri&, const client_response&, const boost::system::error_code& )> handler_func;  
		boost::asio::ip::tcp::resolver resolver_;
		typedef boost::shared_ptr<SocketType> s_type;
		s_type socket_;
		boost::asio::streambuf request_;
		boost::asio::streambuf response_;

		uri uri_;
		std::string body_;
		async_http_client_holder(const uri& u,boost::asio::io_service& io):uri_(u),socket_(new SocketType(io)),resolver_(io){};
		async_http_client_holder(const uri& u,boost::asio::io_service& io,s_type s):uri_(u),socket_(s),resolver_(io){};
		void set_uri(const uri& u){uri_ = u;}
		void get(handler_func f){
			std::ostream request_stream(&request_);
			request_stream << "GET " << uri_.get_uri_client_request_string() << " HTTP/1.0\r\n";
			request_stream << "Host: " << uri_.host() << "\r\n";
			request_stream << "Accept: */*\r\n";
			request_stream << "Connection: close\r\n";
			request_stream << "\r\n";
			request_impl(f);

		}
		void post(const std::string& data,const std::string& content_type,handler_func f){
			std::ostream request_stream(&request_);
			request_stream << "POST " << uri_.get_uri_client_request_string() << " HTTP/1.0\r\n";
			request_stream << "Host: " << uri_.host() << "\r\n";
			request_stream << "Accept: */*\r\n";
			request_stream << "Content-Type: " << content_type;
			request_stream << "Connection: close\r\n\r\n";
			request_impl(f);

		}
		void request_impl(handler_func f){
			std::string port = uri_.port();
			if(port.empty()) port = uri_.schema();
			boost::asio::ip::tcp::resolver::query query(uri_.host(), port);
			auto ptr =  this->shared_from_this();
			resolver_.async_resolve(query,[ptr,f](const boost::system::error_code& err,
				boost::asio::ip::tcp::resolver::iterator endpoint_iterator) -> void
			{
				if (!err)
				{
					// Attempt a connection to each endpoint in the list until we
					// successfully establish a connection.
					boost::asio::async_connect(ptr->socket_->lowest_layer(), endpoint_iterator,[ptr,f](const boost::system::error_code& err,boost::asio::ip::tcp::resolver::iterator iter)
					{
						if (!err)
						{
							// The connection was successful. Do handshake if we need to
							async_do_client_handshake(ptr->uri_.host(), *ptr->socket_,[ptr,f](const boost::system::error_code& err){
								if(!err){

									boost::asio::async_write(*ptr->socket_, ptr->request_,[ptr,f](const boost::system::error_code& err, std::size_t sz){
										if(!err){
											auto sptr = boost::make_shared<jrb_stream_reader<SocketType>>(ptr->socket_,[ptr,f](request& req, response& res, const boost::system::error_code& ec)->bool{return false;});
											sptr->handler_ = [ptr,f](request& req, response& res,const boost::system::error_code& ec)->bool{
												f(ptr->uri_,req,ec);
												return false;
											};
											sptr->start();
										}
										else{
											client_response res;
											f(ptr->uri_,res,err);
										}
									}); //async write
								}
								else{
									client_response res;
									f(ptr->uri_,res,err);
								}

							}); // async_do_client_handshake


						}
						else
						{
							client_response res;
							f(ptr->uri_,res,err);
						}
					}); // async connect

				}
				else
				{
					client_response res;
					f(ptr->uri_,res,err);
				}
			}); // async resolve

		}

	};

#ifdef JRB_NODE_SSL
	namespace{
		struct jrb_client_default_context{
			boost::asio::ssl::context context_;

			jrb_client_default_context():context_(boost::asio::ssl::context::sslv23){
				context_.set_default_verify_paths();
			}

			static jrb_client_default_context& get_default(){
				static jrb_client_default_context def;
				return def;
			}
		};

	}
#endif
	// async_http_client
	async_http_client::async_http_client(const uri& u, boost::asio::io_service& io):uri_(u),io_(&io){}
	void async_http_client::get(handler_func f)const{
#ifdef JRB_NODE_SSL
		if(uri_.schema() == "https"){
			typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;
			boost::shared_ptr<ssl_socket> s(new ssl_socket(*io_,jrb_client_default_context::get_default().context_));
			boost::shared_ptr<async_http_client_holder<ssl_socket>> holder(new async_http_client_holder<ssl_socket>(uri_,*io_,s));
			holder->get(f);
			
		}
		else
#endif
		{
			boost::shared_ptr<async_http_client_holder<boost::asio::ip::tcp::socket>> holder(new async_http_client_holder<boost::asio::ip::tcp::socket>(uri_,*io_));
			holder->get(f);

		}
	}	
	void async_http_client::post(const std::string& data,const std::string& content_type,handler_func f){
#ifdef JRB_NODE_SSL
		if(uri_.schema() == "https"){
			typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;
			boost::shared_ptr<ssl_socket> s(new ssl_socket(*io_,jrb_client_default_context::get_default().context_));
			boost::shared_ptr<async_http_client_holder<ssl_socket>> holder(new async_http_client_holder<ssl_socket>(uri_,*io_,s));
			holder->post(data,content_type,f);
		}
		else
#endif
		{
			boost::shared_ptr<async_http_client_holder<boost::asio::ip::tcp::socket>> holder(new async_http_client_holder<boost::asio::ip::tcp::socket>(uri_,*io_));
			holder->post(data,content_type,f);

		}
	}


	void async_http_client::set_uri(const uri& u){uri_ = u;}
	http_client::http_client(const uri& u):ptr_(new boost::asio::io_service),client_(u,*ptr_){

	}
	client_response http_client::get(){
		boost::promise<std::pair<client_response,boost::system::error_code>> p;
		auto f = p.get_future();
		client_.get([&p](const uri& u,const client_response& r, const boost::system::error_code& err)mutable{
			p.set_value(std::make_pair(r,err));
		});
		boost::thread t;
		if(ptr_){
			t = std::move(boost::thread([this](){ptr_->run();}));
		}
		auto r = f.get();
		t.join();
		if(r.second){
			throw boost::system::system_error(r.second);
		}
		return r.first;
	}

	client_response http_client::post(const std::string& data,const std::string& content_type){
		boost::promise<std::pair<client_response,boost::system::error_code>> p;
		auto f = p.get_future();
		client_.post(data,content_type,[&p](const uri& u,const client_response& r, const boost::system::error_code& err)mutable{
			p.set_value(std::make_pair(r,err));
		});

		auto r = f.get();
		if(r.second){
			throw boost::system::system_error(r.second);
		}
		return r.first;
	}

	





	namespace{
		void set_if_present(http_parser_url& url, std::string& component,http_parser_url_fields field, const std::string& str){
			if(url.field_set & 1 << field){
				component = str.substr(url.field_data[field].off,url.field_data[field].len);
			}
		}

	}

	void uri::set_uri_string(const std::string& str){
		http_parser_url url;
		if(http_parser_parse_url(str.data(),str.size(),0,&url)){
			// error parsing
			throw std::runtime_error("Invalid url");
		}
		set_if_present(url,schema_,UF_SCHEMA,str);
		set_if_present(url,host_,UF_HOST,str);
		set_if_present(url,port_,UF_PORT,str);
		set_if_present(url,path_,UF_PATH,str);
		set_if_present(url,query_,UF_QUERY,str);
		set_if_present(url,fragment_,UF_FRAGMENT,str);


	}

	bool uri::valid(){
		std::string str = get_uri_string();
		http_parser_url url;

		if(http_parser_parse_url(str.data(),str.size(),0,&url)){
			return false;
		}
		else{
			return true;
		}

	}


	namespace status_strings {

		const std::string ok =
			"HTTP/1.0 200 OK\r\n";
		const std::string created =
			"HTTP/1.0 201 Created\r\n";
		const std::string accepted =
			"HTTP/1.0 202 Accepted\r\n";
		const std::string no_content =
			"HTTP/1.0 204 No Content\r\n";
		const std::string multiple_choices =
			"HTTP/1.0 300 Multiple Choices\r\n";
		const std::string moved_permanently =
			"HTTP/1.0 301 Moved Permanently\r\n";
		const std::string moved_temporarily =
			"HTTP/1.0 302 Moved Temporarily\r\n";
		const std::string not_modified =
			"HTTP/1.0 304 Not Modified\r\n";
		const std::string bad_request =
			"HTTP/1.0 400 Bad Request\r\n";
		const std::string unauthorized =
			"HTTP/1.0 401 Unauthorized\r\n";
		const std::string forbidden =
			"HTTP/1.0 403 Forbidden\r\n";
		const std::string not_found =
			"HTTP/1.0 404 Not Found\r\n";
		const std::string internal_server_error =
			"HTTP/1.0 500 Internal Server Error\r\n";
		const std::string not_implemented =
			"HTTP/1.0 501 Not Implemented\r\n";
		const std::string bad_gateway =
			"HTTP/1.0 502 Bad Gateway\r\n";
		const std::string service_unavailable =
			"HTTP/1.0 503 Service Unavailable\r\n";

	}
	namespace misc_strings {

		const std::string name_value_separator = ": ";
		const std::string crlf = "\r\n";


	} // namespace misc_strings

	std::string response_derived::get_as_http()
	{
		add_required_headers();
		std::vector<std::string> buffers;
		buffers.push_back(status_.get_status_http_string());
		for(auto p: message_.headers())
		{
			buffers.push_back((p.first));
			buffers.push_back((misc_strings::name_value_separator));
			buffers.push_back((p.second));
			buffers.push_back((misc_strings::crlf));
		}
		buffers.push_back((misc_strings::crlf));
		buffers.push_back((message_.body()));
		return boost::algorithm::join(buffers,"");

	}

	const std::string& status_t::get_status_http_string(status_t::status_type s)const{

		using namespace status_strings;
		switch (s)
		{
		case status_t::ok:
			return status_strings::ok;
		case status_t::created:
			return status_strings::created;
		case status_t::accepted:
			return status_strings::accepted;
		case status_t::no_content:
			return status_strings::no_content;
		case status_t::multiple_choices:
			return status_strings::multiple_choices;
		case status_t::moved_permanently:
			return status_strings::moved_permanently;
		case status_t::moved_temporarily:
			return status_strings::moved_temporarily;
		case status_t::not_modified:
			return status_strings::not_modified;
		case status_t::bad_request:
			return status_strings::bad_request;
		case status_t::unauthorized:
			return status_strings::unauthorized;
		case status_t::forbidden:
			return status_strings::forbidden;
		case status_t::not_found:
			return status_strings::not_found;
		case status_t::internal_server_error:
			return status_strings::internal_server_error;
		case status_t::not_implemented:
			return status_strings::not_implemented;
		case status_t::bad_gateway:
			return status_strings::bad_gateway;
		case status_t::service_unavailable:
			return status_strings::service_unavailable;
		default:
			return status_strings::internal_server_error;
		}
	}

}




