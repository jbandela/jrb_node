//  Copyright John R. Bandela 2012
//
// Distributed under the Boost Software License, Version 1.0.
//    (See http://www.boost.org/LICENSE_1_0.txt)

#ifndef JRB_NODE_NAME_VALUE_HPP_12_07_2012
#define JRB_NODE_NAME_VALUE_HPP_12_07_2012

#include <string>
#include <exception>
#include <iterator>
#include <algorithm>
#include <vector>
#include <boost/algorithm/string.hpp>

namespace jrb_node{

	namespace detail{

		inline std::string hex_encode(unsigned char c){
			static const char nibble[] = "0123456789ABCDEF";
			std::string ret;
			ret += nibble[c >> 4];
			ret += nibble[c & 0x0f];
			return ret;
		}
		inline unsigned char hex_decode(const std::string& s){
			static const char nibble[] = "0123456789ABCDEF";
			if(s.size() < 2) throw std::runtime_error("Invalid hex string");
			auto iter1 = std::find(std::begin(nibble),std::end(nibble),s[0]);
			if(iter1 == std::end(nibble))throw std::runtime_error("Invalid hex string");
			auto iter2 = std::find(std::begin(nibble),std::end(nibble),s[1]);
			if(iter2 == std::end(nibble))throw std::runtime_error("Invalid hex string");
			unsigned char c1 = iter1 - std::begin(nibble);
			unsigned char c2 = iter2 - std::begin(nibble);
			unsigned char c = (c1 << 4) | c2;
			return c;
		}

	}

	template<class InIt,class OutIt>
	void url_encode(InIt begin, InIt end, OutIt out){
		static const char safe[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
		for(auto iter = begin; iter != end; ++iter){
			auto f = std::find(std::begin(safe),std::end(safe),*iter);
			if(f != std::end(safe)){ *out++ = *f;}
			else{
				std::string hex = detail::hex_encode(*iter);
				hex = "%" + hex;
				std::copy(hex.begin(),hex.end(),out);
			}

		}


	}
	template<class InIt, class OutIt>
	void url_decode(InIt begin, InIt end, OutIt out){
		for(auto iter = begin; iter != end; ++iter){
			if(*iter=='%'){
				++iter;
				auto b = iter;
				if(iter == end){throw std::runtime_error("Invalid hex string");}
				++iter;
				if(iter == end){throw std::runtime_error("Invalid hex string");}
				auto e = iter;
				++e;
				std::string hex_string(b,e);
				*out++ = detail::hex_decode(hex_string);
			}
			else{
				*out++ = *iter;
			}



		}


	}

	inline std::string url_encode(const std::string& s){
		std::string ret;
		url_encode(s.begin(),s.end(),std::back_inserter(ret));
		return ret;


	}
	inline std::string url_decode(const std::string& s){
		std::string ret;
		url_decode(s.begin(),s.end(),std::back_inserter(ret));
		return ret;


	}


	template<class MapType>
	void parse_name_value(const std::string& s, MapType& m){
		std::vector<std::string> pairs;
		boost::algorithm::split(pairs,s,[](char c){return c=='&';});
		for(const auto& p:pairs){
			std::vector<std::string> pair;
			boost::algorithm::split(pair,p,[](char c){return c=='=';});
			if(!pair.size()) continue;
			if(!pair[0].size())continue;
			if(pair.size() < 2)pair.push_back(std::string());
			m[url_decode(pair[0])] = url_decode(pair[1]);

		}

	}

	template<class MapType>
    std::string name_value_to_string(MapType& m, bool sort = false){
		std::vector<std::string> pairs;
		for(const auto& p:m){
			pairs.push_back(url_encode(p.first) + "=" + url_encode(p.second));
		}
		if(sort)std::sort(pairs.begin(),pairs.end());
		return boost::algorithm::join(pairs,"&");
	}







}













#endif