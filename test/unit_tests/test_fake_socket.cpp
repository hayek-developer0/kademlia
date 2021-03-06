// Copyright (c) 2014, David Keller
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the University of California, Berkeley nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY DAVID KELLER AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "common.hpp"

#include <numeric>

#include <boost/asio/io_service.hpp>

#include "kademlia/buffer.hpp"
#include "fake_socket.hpp"

namespace {

namespace k = kademlia;
namespace kd = k::detail;
namespace a = boost::asio;

BOOST_AUTO_TEST_SUITE( test_construction )

BOOST_AUTO_TEST_CASE( can_be_created )
{
    a::io_service io_service;
    k::test::fake_socket s( io_service
                           , a::ip::udp::endpoint().protocol() );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );
}

BOOST_AUTO_TEST_CASE( does_not_invoke_receive_callback_until_data_is_received )
{
    a::io_service io_service;
    boost::asio::ip::udp::endpoint endpoint;
    k::test::fake_socket s( io_service, endpoint.protocol() );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer buffer( 32 );

    auto on_receive = []
        ( boost::system::error_code const&
        , std::size_t )
    { BOOST_FAIL( "unexpected call" ); };

    s.async_receive_from( boost::asio::buffer( buffer )
                        , endpoint
                        , on_receive );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );
}

BOOST_AUTO_TEST_CASE( invokes_send_callback_when_host_is_unreachable )
{
    a::io_service io_service;
    boost::asio::ip::udp::endpoint endpoint;
    k::test::fake_socket s( io_service, endpoint.protocol() );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer buffer( 32 );

    auto on_send = []
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        BOOST_REQUIRE( failure );
        BOOST_REQUIRE_EQUAL( 0, bytes_count );
    };

    s.async_send_to( boost::asio::buffer( buffer )
                   , endpoint
                   , on_send );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );
}

BOOST_AUTO_TEST_CASE( can_send_and_receive_messages )
{
    a::io_service io_service;
    a::io_service::work work(io_service);
    boost::asio::ip::udp::endpoint endpoint;
    endpoint.port( k::test::fake_socket::FIXED_PORT );

    k::test::fake_socket receiver( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! receiver.bind( endpoint ) );

    k::test::fake_socket sender( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! sender.bind( endpoint ) );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer received( 64 );
    kd::buffer sent( 32 );

    bool receive_callback_called = false;
    auto on_receive = [ &receive_callback_called, &sent ]
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        receive_callback_called = true;
        BOOST_REQUIRE( ! failure );
        BOOST_REQUIRE_EQUAL( sent.size(), bytes_count );
    };

    receiver.async_receive_from( boost::asio::buffer( received )
                               , endpoint
                               , on_receive );

    std::iota( sent.begin(), sent.end(), 1 );
    auto on_send = [ &sent, &received ]
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        BOOST_REQUIRE( ! failure );
        BOOST_REQUIRE_EQUAL( sent.size(), bytes_count );
        received.resize( bytes_count );
    };

    sender.async_send_to( boost::asio::buffer( sent )
                        , receiver.local_endpoint()
                        , on_send );

    BOOST_REQUIRE_LT( 0ULL, io_service.poll() );
    BOOST_REQUIRE( receive_callback_called );
    BOOST_REQUIRE_EQUAL_COLLECTIONS( sent.begin(), sent.end()
                                   , received.begin(), received.end() );
}

BOOST_AUTO_TEST_CASE( can_detect_invalid_address )
{
    a::io_service io_service;
    boost::asio::ip::udp::endpoint endpoint;
    endpoint.port( k::test::fake_socket::FIXED_PORT );

    k::test::fake_socket s( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! s.bind( endpoint ) );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer sent( 32 );
    std::iota( sent.begin(), sent.end(), 1 );

    bool send_callback_called = false;
    auto on_send = [ &send_callback_called ]
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        BOOST_REQUIRE( failure );
        BOOST_REQUIRE_EQUAL( 0ULL, bytes_count );
        send_callback_called = true;
    };

    endpoint.address( boost::asio::ip::address_v4::any() );

    s.async_send_to( boost::asio::buffer( sent )
                   , endpoint, on_send );

    BOOST_REQUIRE_LE( 0ULL, io_service.poll() );
    BOOST_REQUIRE( send_callback_called );
}

BOOST_AUTO_TEST_CASE( can_detect_closed_socket )
{
    a::io_service io_service;
    a::io_service::work work(io_service);
    boost::asio::ip::udp::endpoint endpoint;
    endpoint.port( k::test::fake_socket::FIXED_PORT );

    k::test::fake_socket receiver( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! receiver.bind( endpoint ) );

    k::test::fake_socket sender( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! sender.bind( endpoint ) );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer received( 64 );
    kd::buffer sent( 32 );

    auto on_receive = []
        ( boost::system::error_code const& 
        , std::size_t )
    { };

    receiver.async_receive_from( boost::asio::buffer( received )
                               , endpoint
                               , on_receive );

    std::iota( sent.begin(), sent.end(), 1 );
    auto on_send = []
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        BOOST_REQUIRE( failure );
        BOOST_REQUIRE_EQUAL( 0ULL, bytes_count );
    };

    boost::system::error_code failure;
    receiver.close( failure );
    BOOST_REQUIRE(! failure );

    sender.async_send_to( boost::asio::buffer( sent )
                        , receiver.local_endpoint()
                        , on_send );

    BOOST_REQUIRE_LE( 0ULL, io_service.poll() );
}

BOOST_AUTO_TEST_CASE( can_send_and_receive_messages_to_self )
{
    a::io_service io_service;
    a::io_service::work work(io_service);
    boost::asio::ip::udp::endpoint endpoint;
    endpoint.port( k::test::fake_socket::FIXED_PORT );

    k::test::fake_socket sender( io_service, endpoint.protocol() );
    BOOST_REQUIRE(! sender.bind( endpoint ) );

    BOOST_REQUIRE_EQUAL( 0ULL, io_service.poll() );

    kd::buffer received( 64 );
    kd::buffer sent( 32 );

    bool receive_callback_called = false;
    auto on_receive = [ &receive_callback_called, &sent ]
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        receive_callback_called = true;
        BOOST_REQUIRE( ! failure );
        BOOST_REQUIRE_EQUAL( sent.size(), bytes_count );
    };

    sender.async_receive_from( boost::asio::buffer( received )
                             , endpoint
                             , on_receive );

    std::iota( sent.begin(), sent.end(), 1 );
    auto on_send = [ &sent, &received ]
        ( boost::system::error_code const& failure
        , std::size_t bytes_count )
    {
        BOOST_REQUIRE( ! failure );
        BOOST_REQUIRE_EQUAL( sent.size(), bytes_count );
        received.resize( bytes_count );
    };

    sender.async_send_to( boost::asio::buffer( sent )
                        , sender.local_endpoint()
                        , on_send );

    BOOST_REQUIRE_LT( 0ULL, io_service.poll() );
    BOOST_REQUIRE( receive_callback_called );
    BOOST_REQUIRE_EQUAL_COLLECTIONS( sent.begin(), sent.end()
                                   , received.begin(), received.end() );
}


BOOST_AUTO_TEST_SUITE_END()

}

